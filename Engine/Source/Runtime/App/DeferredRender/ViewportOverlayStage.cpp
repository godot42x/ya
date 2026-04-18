#include "ViewportOverlayStage.h"

#include "Core/Math/Math.h"
#include "ECS/Component/DirectionComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/IRenderTarget.h"

#include "Resource/PrimitiveMeshCache.h"
#include "Runtime/App/App.h"
#include "Runtime/App/RenderRuntime.h"

#include "Scene/SceneManager.h"


#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

void ViewportOverlayStage::refreshPipelineFormats(const IRenderTarget* viewportRT)
{
    if (!viewportRT) {
        return;
    }

    const auto& colorDescs = viewportRT->getColorAttachmentDescs();
    const auto& depthDesc  = viewportRT->getDepthAttachmentDesc();
    if (colorDescs.empty()) {
        return;
    }

    const auto colorFormat = colorDescs.front().format;
    const auto depthFormat = depthDesc.has_value() ? depthDesc->format : EFormat::Undefined;

    if (_skyboxPipeline) {
        auto ci                                         = _skyboxPipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _skyboxPipeline->updateDesc(std::move(ci));
    }

    if (_overlayPipeline) {
        auto ci                                         = _overlayPipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _overlayPipeline->updateDesc(std::move(ci));
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Init
// ═══════════════════════════════════════════════════════════════════════

void ViewportOverlayStage::init(IRender* render)
{
    _render = render;
    initSkybox();
    initOverlay();
}

void ViewportOverlayStage::initSkybox()
{
    // DSLs
    auto dsls          = IDescriptorSetLayout::create(_render, {
                                                          DescriptorSetLayoutDesc{
                                                                       .label    = "SkyboxOverlay_PerFrame_DSL",
                                                                       .set      = 0,
                                                                       .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
                                                          },
                                                          DescriptorSetLayoutDesc{
                                                                       .label    = "SkyboxOverlay_Resource_DSL",
                                                                       .set      = 1,
                                                                       .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
                                                          },
                                                      });
    _skyboxFrameDSL    = dsls[0];
    _skyboxResourceDSL = dsls[1];

    // Pipeline layout
    _skyboxPPL = IPipelineLayout::create(_render, "SkyboxOverlay_PPL", {}, {_skyboxFrameDSL, _skyboxResourceDSL});

    // Pipeline
    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {
            .label                  = "Deferred Skybox Overlay",
            .colorAttachmentFormats = {LINEAR_FORMAT},
            .depthAttachmentFormat  = DEPTH_FORMAT,
        },
        .pipelineLayout = _skyboxPPL.get(),
        .shaderDesc     = ShaderDesc{
                .shaderName        = "Skybox.glsl",
                .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
                .vertexAttributes  = {
                {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
                {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
                {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
            },
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = false, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {{.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _skyboxPipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_skyboxPipeline && _skyboxPipeline->recreate(ci), "Failed to create Skybox overlay pipeline");

    // Per-flight UBO + DS
    _skyboxDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                      .label     = "SkyboxOverlay_DSP",
                                                      .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
                                                      .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
                                                  });

    SkyboxFrameUBO initialData{};
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _skyboxFrameUBO[i] = IBuffer::create(_render, BufferCreateInfo{
                                                          .label       = std::format("SkyboxOverlay_Frame_UBO_{}", i),
                                                          .usage       = EBufferUsage::UniformBuffer,
                                                          .size        = sizeof(SkyboxFrameUBO),
                                                          .memoryUsage = EMemoryUsage::CpuToGpu,
                                                      });
        _skyboxFrameUBO[i]->writeData(&initialData, sizeof(SkyboxFrameUBO), 0);

        _skyboxFrameDS[i] = _skyboxDSP->allocateDescriptorSets(_skyboxFrameDSL);
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(_skyboxFrameDS[i], 0, _skyboxFrameUBO[i].get()),
        });
    }
}

void ViewportOverlayStage::initOverlay()
{
    constexpr auto pcSize = sizeof(OverlayPushConstant);
    _overlayPPL           = IPipelineLayout::create(
        _render, "SimpleMaterialOverlay_PPL", {PushConstantRange{.offset = 0, .size = pcSize, .stageFlags = EShaderStage::Vertex}}, {});

    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {
            .label                  = "Deferred SimpleMaterial Overlay",
            .colorAttachmentFormats = {LINEAR_FORMAT},
            .depthAttachmentFormat  = DEPTH_FORMAT,
        },
        .pipelineLayout = _overlayPPL.get(),
        .shaderDesc     = ShaderDesc{
                .shaderName        = "Test/SimpleMaterial.glsl",
                .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
                .vertexAttributes  = {
                {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
                {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
                {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
            },
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
        .colorBlendState    = {.attachments = {{.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _overlayPipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_overlayPipeline && _overlayPipeline->recreate(ci), "Failed to create SimpleMaterial overlay pipeline");
}

void ViewportOverlayStage::destroy()
{
    _skyboxPipeline.reset();
    _skyboxPPL.reset();
    _skyboxFrameDSL.reset();
    _skyboxResourceDSL.reset();
    _skyboxDSP.reset();
    for (auto& ubo : _skyboxFrameUBO) ubo.reset();

    _overlayPipeline.reset();
    _overlayPPL.reset();
    _render = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Prepare
// ═══════════════════════════════════════════════════════════════════════

void ViewportOverlayStage::prepare(const RenderStageContext& ctx)
{
    if (_skyboxPipeline) {
        _skyboxPipeline->beginFrame();
    }
    if (_overlayPipeline) {
        _overlayPipeline->beginFrame();
    }

    if (!ctx.frameData) return;

    // Update skybox frame UBO (view without translation + projection)
    SkyboxFrameUBO uboData{
        .projection = ctx.frameData->projection,
        .view       = FMath::dropTranslation(ctx.frameData->view),
    };
    _skyboxFrameUBO[ctx.flightIndex]->writeData(&uboData, sizeof(SkyboxFrameUBO), 0);
}

// ═══════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════

void ViewportOverlayStage::execute(const RenderStageContext& ctx)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    drawSkybox(ctx);
    drawOverlay(ctx);
}

void ViewportOverlayStage::drawSkybox(const RenderStageContext& ctx)
{
    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) return;

    // Check if skybox is available
    auto* resolver = App::get()->getResourceResolveSystem();
    auto* scene    = App::get()->getSceneManager()->getActiveScene();
    if (!resolver || !scene) return;

    const auto* skyboxState = resolver->findFirstSceneSkyboxState(scene);
    if (!skyboxState || !skyboxState->hasRenderableCubemap()) return;

    auto* runtime = App::get()->getRenderRuntime();
    if (!runtime) return;
    auto skyboxDS = runtime->getSceneSkyboxDescriptorSet(scene);

    // Get a cube mesh from the scene's skybox entity, or use primitive cache
    Mesh* cubeMesh = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube);
    // Try scene mesh first
    for (const auto& [entity, sc, mc] : scene->getRegistry().view<SkyboxComponent, MeshComponent>().each()) {
        if (mc.isResolved() && mc.getMesh()) {
            cubeMesh = mc.getMesh();
        }
        break;
    }
    if (!cubeMesh) return;

    cmdBuf->debugBeginLabel("Skybox");

    cmdBuf->bindPipeline(_skyboxPipeline.get());

    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(vpH);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(vpH);
        viewportHeight = -static_cast<float>(vpH);
    }
    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(vpW), viewportHeight, 0.0f, 1.0f);
    cmdBuf->setScissor(0, 0, vpW, vpH);

    cmdBuf->bindDescriptorSets(_skyboxPPL.get(), 0, {_skyboxFrameDS[ctx.flightIndex], skyboxDS});
    cubeMesh->draw(cmdBuf);

    cmdBuf->debugEndLabel();
}

void ViewportOverlayStage::drawOverlay(const RenderStageContext& ctx)
{
    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) return;

    auto* scene = App::get()->getSceneManager()->getActiveScene();
    if (!scene) return;

    const auto& fd = *ctx.frameData;

    // Simple material entities (from snapshot)
    bool hasSimple = !fd.simpleDrawItems.empty();

    // Direction components (still from registry — editor visualization, TODO: migrate to snapshot)
    const auto& dirView      = scene->getRegistry().view<TransformComponent, DirectionComponent>();
    bool        hasDirection = dirView.begin() != dirView.end();

    if (!hasSimple && !hasDirection) return;

    cmdBuf->debugBeginLabel("ForwardOverlay");

    cmdBuf->bindPipeline(_overlayPipeline.get());

    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(vpH);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(vpH);
        viewportHeight = -static_cast<float>(vpH);
    }
    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(vpW), viewportHeight);
    cmdBuf->setScissor(0, 0, vpW, vpH);

    _overlayPC.view       = fd.view;
    _overlayPC.projection = fd.projection;

    // Draw simple material entities from snapshot
    for (const auto& item : fd.simpleDrawItems) {
        if (!item.mesh || !item.material) continue;
        auto* mat            = static_cast<SimpleMaterial*>(item.material);
        _overlayPC.model     = item.worldMatrix;
        _overlayPC.colorType = mat->colorType;
        cmdBuf->pushConstants(_overlayPPL.get(), EShaderStage::Vertex, 0, sizeof(OverlayPushConstant), &_overlayPC);
        item.mesh->draw(cmdBuf);
    }

    // Draw direction cones/cylinders (from registry — editor debug vis)
    // TODO: migrate DirectionComponent visualization to RenderFrameData
    auto* cone     = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cone);
    auto* cylinder = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cylinder);

    glm::mat4 coneLocalTransf =
        glm::rotate(glm::mat4(1.0), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0), glm::vec3(0.3f, 1.0f, 0.3f));
    glm::mat4 cylinderLocalTransf =
        glm::rotate(glm::mat4(1.0), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0), glm::vec3(0.1f, 1.0f, 0.1f));

    _overlayPC.colorType = _defaultColorType;
    for (auto entity : dirView) {
        const auto& [tc, dc] = dirView.get(entity);

        glm::mat4 worldTransform = glm::translate(glm::mat4(1.0), tc.getWorldPosition()) *
                                   glm::mat4_cast(glm::quat(glm::radians(tc.getRotation())));

        _overlayPC.model = glm::translate(glm::mat4(1.0), -tc.getForward()) * coneLocalTransf * worldTransform;
        cmdBuf->pushConstants(_overlayPPL.get(), EShaderStage::Vertex, 0, sizeof(OverlayPushConstant), &_overlayPC);
        cone->draw(cmdBuf);

        _overlayPC.model = worldTransform * cylinderLocalTransf;
        cmdBuf->pushConstants(_overlayPPL.get(), EShaderStage::Vertex, 0, sizeof(OverlayPushConstant), &_overlayPC);
        cylinder->draw(cmdBuf);
    }

    cmdBuf->debugEndLabel();
}

// ═══════════════════════════════════════════════════════════════════════
// GUI
// ═══════════════════════════════════════════════════════════════════════

void ViewportOverlayStage::renderGUI()
{
    if (!ImGui::TreeNode("Viewport Overlay Stage")) {
        return;
    }
    // Future: skybox toggle, overlay color type combo, etc.
    _skyboxPipeline->renderGUI();
    _overlayPipeline->renderGUI();

    ImGui::TreePop();
}

} // namespace ya
