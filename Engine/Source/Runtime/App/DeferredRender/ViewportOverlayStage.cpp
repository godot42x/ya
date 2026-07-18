#include "ViewportOverlayStage.h"

#include "Core/Profiling/Instrumentor.h"

#include "Core/Math/Math.h"
#include "ECS/Component/DirectionComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/RenderResourceFactory.h"

#include "Resource/Mesh/PrimitiveMeshCache.h"

#include "Scene/Scene.h"


#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

namespace
{

bool hasDebugSkinningDrawItem(const std::vector<RenderDrawItem>& items)
{
    return std::ranges::any_of(items, [](const RenderDrawItem& item)
                               { return item.mesh && item.mesh->hasSkinningVertexBuffer(); });
}

void drawDebugSkinningItems(DebugSkinning&                     debugSkinning,
                            ICommandBuffer*                    cmdBuf,
                            const std::vector<RenderDrawItem>& items,
                            uint32_t                           vpW,
                            uint32_t                           vpH,
                            const RenderFrameData&             fd)
{
    for (const auto& item : items) {
        if (!item.mesh || !item.mesh->hasSkinningVertexBuffer()) continue;
        debugSkinning.draw(cmdBuf,
                           item.mesh,
                           vpW,
                           vpH,
                           fd.projection,
                           fd.view,
                           item.worldMatrix);
    }
}

} // namespace

void ViewportOverlayStage::setServices(Services services)
{
    _getDebugRenderSystem        = std::move(services.getDebugRenderSystem);
}

void ViewportOverlayStage::setFrameInputs(FrameInputs frameInputs)
{
    _frameInputs = std::move(frameInputs);
}

void ViewportOverlayStage::refreshPipelineFormats(const DeferredAttachmentFormats& formats)
{
    if (!formats.hasColor()) {
        return;
    }
    const auto colorFormat = formats.colorFormats.front();
    const auto depthFormat = formats.depthFormat.value_or(EFormat::Undefined);

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

    if (_debugRenderSystem) {
        _debugRenderSystem->refreshPipelineFormats(formats);
    }
    _debugSkinning.refreshPipelineFormats(formats);
}

// ═══════════════════════════════════════════════════════════════════════
// Init
// ═══════════════════════════════════════════════════════════════════════

void ViewportOverlayStage::init(IRender* render)
{
    _render = render;
    _directionCone = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cone);
    _directionCylinder = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cylinder);
    YA_CORE_ASSERT(_directionCone != nullptr, "ViewportOverlayStage requires direction cone mesh");
    YA_CORE_ASSERT(_directionCylinder != nullptr, "ViewportOverlayStage requires direction cylinder mesh");
    initSkybox();
    initOverlay();
    YA_CORE_ASSERT(_getDebugRenderSystem, "ViewportOverlayStage requires debug render system service");
    _debugRenderSystem = &_getDebugRenderSystem();
    YA_CORE_ASSERT(_debugRenderSystem != nullptr, "ViewportOverlayStage requires debug render system instance");
    _debugRenderSystem->init(_render);
    _debugRenderSystem->setReverseViewportY(bReverseViewportY);
    _debugSkinning.init(_render);
    _debugSkinning.bReverseViewportY = bReverseViewportY;
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
        _skyboxFrameUBO[i] = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
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
    _directionCone = nullptr;
    _directionCylinder = nullptr;
    _debugRenderSystem = nullptr;
    _debugSkinning.destroy();
    _getDebugRenderSystem        = {};
    _frameInputs                 = {};
    _render = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Prepare
// ═══════════════════════════════════════════════════════════════════════

void ViewportOverlayStage::prepare(const RenderStageContext& ctx)
{
    YA_PROFILE_FUNCTION();
    if (_skyboxPipeline) {
        _skyboxPipeline->beginFrame();
    }
    if (_overlayPipeline) {
        _overlayPipeline->beginFrame();
    }
    if (_debugRenderSystem) {
        _debugRenderSystem->beginFrame();
    }
    _debugSkinning.beginFrame();

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
    YA_PROFILE_FUNCTION();
    if (!ctx.cmdBuf || !ctx.frameData) return;

    executeSkybox(ctx);
    executeOverlay(ctx);
}

void ViewportOverlayStage::executeSkybox(const RenderStageContext& ctx)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    drawSkybox(ctx);
}

void ViewportOverlayStage::executeOverlay(const RenderStageContext& ctx)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    drawOverlay(ctx);
}

void ViewportOverlayStage::drawSkybox(const RenderStageContext& ctx)
{
    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) return;

    // Check if skybox is available
    if (!_frameInputs.skybox.bAvailable || !_frameInputs.skybox.mesh) return;

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

    cmdBuf->bindDescriptorSets(_skyboxPPL.get(), 0, {_skyboxFrameDS[ctx.flightIndex], _frameInputs.skybox.descriptorSet});
    _frameInputs.skybox.mesh->draw(cmdBuf);

    cmdBuf->debugEndLabel();
}

void ViewportOverlayStage::drawOverlay(const RenderStageContext& ctx)
{
    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) return;

    const auto& fd                     = *ctx.frameData;
    if (!_debugRenderSystem) {
        return;
    }
    auto& debugSystem = *_debugRenderSystem;
    debugSystem.setReverseViewportY(bReverseViewportY);
    _debugSkinning.bReverseViewportY   = bReverseViewportY;

    // Simple material entities (from snapshot)
    const auto& staticBuckets  = fd.drawBuckets.staticMeshes;
    const auto& skinnedBuckets = fd.drawBuckets.skinnedMeshes;
    bool hasSimple = !staticBuckets.simpleDrawItems.empty() || !skinnedBuckets.simpleDrawItems.empty();

    bool hasDebugSkinning = _debugSkinning.bEnabled &&
                            (hasDebugSkinningDrawItem(skinnedBuckets.phongDrawItems) ||
                             hasDebugSkinningDrawItem(skinnedBuckets.simpleDrawItems) ||
                             hasDebugSkinningDrawItem(skinnedBuckets.fallbackDrawItems) ||
                             hasDebugSkinningDrawItem(skinnedBuckets.pbrDrawItems));

    const bool hasDirection = !_frameInputs.directionGizmos.empty();

    if (!hasSimple && !hasDirection && !hasDebugSkinning) return;

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
    auto drawSimpleBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            auto* mat            = static_cast<SimpleMaterial*>(item.material);
            _overlayPC.model     = item.worldMatrix;
            _overlayPC.colorType = mat->colorType;
            cmdBuf->pushConstants(_overlayPPL.get(), EShaderStage::Vertex, 0, sizeof(OverlayPushConstant), &_overlayPC);
            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };
    drawSimpleBucket(staticBuckets.simpleDrawItems, false);
    drawSimpleBucket(skinnedBuckets.simpleDrawItems, true);

    if (_debugSkinning.bEnabled) {
        drawDebugSkinningItems(_debugSkinning, cmdBuf, skinnedBuckets.phongDrawItems, vpW, vpH, fd);
        drawDebugSkinningItems(_debugSkinning, cmdBuf, skinnedBuckets.simpleDrawItems, vpW, vpH, fd);
        drawDebugSkinningItems(_debugSkinning, cmdBuf, skinnedBuckets.fallbackDrawItems, vpW, vpH, fd);
        drawDebugSkinningItems(_debugSkinning, cmdBuf, skinnedBuckets.pbrDrawItems, vpW, vpH, fd);
    }

    // Draw direction cones/cylinders from setup snapshot
    if (hasDirection && (!_directionCone || !_directionCylinder)) return;

    _overlayPC.colorType = _defaultColorType;
    for (const auto& gizmo : _frameInputs.directionGizmos) {
        _overlayPC.model = gizmo.coneModel;
        cmdBuf->pushConstants(_overlayPPL.get(), EShaderStage::Vertex, 0, sizeof(OverlayPushConstant), &_overlayPC);
        _directionCone->draw(cmdBuf);

        _overlayPC.model = gizmo.cylinderModel;
        cmdBuf->pushConstants(_overlayPPL.get(), EShaderStage::Vertex, 0, sizeof(OverlayPushConstant), &_overlayPC);
        _directionCylinder->draw(cmdBuf);

        debugSystem.addConeImmediate(gizmo.coneModel,
                                     glm::vec4(0.9f, 0.6f, 0.1f, 1.0f));
        debugSystem.addCylinderImmediate(gizmo.cylinderModel,
                                         glm::vec4(0.1f, 0.9f, 0.9f, 1.0f));
        debugSystem.addLineImmediate(gizmo.lineStart, gizmo.lineEnd, glm::vec4(1.0f, 0.2f, 0.2f, 1.0f));
    }

    debugSystem.draw(cmdBuf, vpW, vpH, fd.projection, fd.view);

    cmdBuf->debugEndLabel();
}

// ═══════════════════════════════════════════════════════════════════════
// GUI
// ═══════════════════════════════════════════════════════════════════════

void ViewportOverlayStage::renderGUI()
{
}

} // namespace ya
