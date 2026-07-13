#include "ForwardViewportAuxPasses.h"

#include "Core/Math/Math.h"
#include "ECS/Component/DirectionComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Scene/Scene.h"

#include "glm/gtc/type_ptr.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"

namespace ya
{

namespace
{

static const std::vector<VertexAttribute> kAuxVertexAttributes3 = {
    {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
    {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
    {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
};

static const VertexBufferDescription kAuxVBDesc{.slot = 0, .pitch = sizeof(ya::Vertex)};

} // namespace

void ForwardViewportAuxPasses::init(const InitDesc& desc)
{
    _render                = desc.render;
    _getElapsedTimeSeconds = desc.getElapsedTimeSeconds;
    initSimple(desc);
    initSkybox(desc);
    initDebug(desc);
}

void ForwardViewportAuxPasses::destroy()
{
    _simplePipeline.reset();
    _simplePPL.reset();

    _skyboxPipeline.reset();
    _skyboxPPL.reset();
    _skyboxFrameDSL.reset();
    _skyboxResourceDSL.reset();
    _skyboxDSP.reset();
    for (auto& u : _skyboxFrameUBO) u.reset();

    _debugPipeline.reset();
    _debugPPL.reset();
    _debugDSL.reset();
    _debugDSP.reset();
    _debugUboBuffer.reset();

    _getElapsedTimeSeconds = {};
    _render = nullptr;
}

void ForwardViewportAuxPasses::beginFrame()
{
    if (_simplePipeline) {
        _simplePipeline->beginFrame();
    }
    if (_skyboxPipeline) {
        _skyboxPipeline->beginFrame();
    }
    if (_debugPipeline) {
        _debugPipeline->beginFrame();
    }
}

void ForwardViewportAuxPasses::refreshPipelineFormats(const RenderAttachmentFormats& formats)
{
    if (!formats.hasColor()) {
        return;
    }

    if (_simplePipeline) {
        auto ci                                         = _simplePipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {formats.colorFormats.front()};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = formats.depthFormat.value_or(EFormat::Undefined);
        _simplePipeline->updateDesc(std::move(ci));
    }

    if (_skyboxPipeline) {
        auto ci                                         = _skyboxPipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {formats.colorFormats.front()};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = formats.depthFormat.value_or(EFormat::Undefined);
        _skyboxPipeline->updateDesc(std::move(ci));
    }

    if (_debugPipeline) {
        _debugPipelineCI.pipelineRenderingInfo.colorAttachmentFormats = {formats.colorFormats.front()};
        _debugPipelineCI.pipelineRenderingInfo.depthAttachmentFormat  = formats.depthFormat.value_or(EFormat::Undefined);
        _debugPipeline->updateDesc(_debugPipelineCI);
    }
}

void ForwardViewportAuxPasses::prepare(const RenderStageContext& ctx)
{
    if (!ctx.frameData) {
        return;
    }

    SkyboxFrameUBO skyboxUBO{
        .projection = ctx.frameData->projection,
        .view       = FMath::dropTranslation(ctx.frameData->view),
    };
    _skyboxFrameUBO[ctx.flightIndex]->writeData(&skyboxUBO, sizeof(SkyboxFrameUBO), 0);
}

void ForwardViewportAuxPasses::initSimple(const InitDesc& desc)
{
    _simplePPL = IPipelineLayout::create(
        _render, "FwdSimple_PPL", {PushConstantRange{.offset = 0, .size = sizeof(SimplePC), .stageFlags = EShaderStage::Vertex}}, {});

    GraphicsPipelineCreateInfo ci{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _simplePPL.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "Test/SimpleMaterial.glsl",
            .vertexBufferDescs = {kAuxVBDesc},
            .vertexAttributes  = kAuxVertexAttributes3,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Scissor, EPipelineDynamicFeature::Viewport},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .frontFace = EFrontFaceType::CounterClockWise},
        .multisampleState   = {.sampleCount = ESampleCount::Sample_1},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
        .colorBlendState    = {.attachments = {{
                                   .index               = 0,
                                   .bBlendEnable        = false,
                                   .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                                   .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                                   .colorBlendOp        = EBlendOp::Add,
                                   .srcAlphaBlendFactor = EBlendFactor::One,
                                   .dstAlphaBlendFactor = EBlendFactor::Zero,
                                   .alphaBlendOp        = EBlendOp::Add,
                                   .colorWriteMask      = static_cast<EColorComponent::T>(EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A),
                               }}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _simplePipeline = IGraphicsPipeline::create(_render);
    _simplePipeline->recreate(ci);
}

void ForwardViewportAuxPasses::initSkybox(const InitDesc& desc)
{
    auto dsls = IDescriptorSetLayout::create(_render, {
        DescriptorSetLayoutDesc{
            .label    = "FwdSkybox_PerFrame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        },
        DescriptorSetLayoutDesc{
            .label    = "FwdSkybox_Resource_DSL",
            .set      = 1,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
        },
    });
    _skyboxFrameDSL    = dsls[0];
    _skyboxResourceDSL = dsls[1];

    _skyboxPPL = IPipelineLayout::create(_render, "FwdSkybox_PPL", {}, dsls);

    GraphicsPipelineCreateInfo ci{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _skyboxPPL.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "Skybox.glsl",
            .vertexBufferDescs = {kAuxVBDesc},
            .vertexAttributes  = kAuxVertexAttributes3,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = false, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {{.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _skyboxPipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_skyboxPipeline && _skyboxPipeline->recreate(ci), "Failed to create Forward Skybox pipeline");

    _skyboxDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
        .label     = "FwdSkybox_DSP",
        .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
        .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
    });

    SkyboxFrameUBO initialData{};
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _skyboxFrameUBO[i] = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
            .label       = std::format("FwdSkybox_Frame_UBO_{}", i),
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

void ForwardViewportAuxPasses::initDebug(const InitDesc& desc)
{
    _debugDSL = IDescriptorSetLayout::create(_render,
                                             DescriptorSetLayoutDesc{
                                                 .label    = "FwdDebug_DSL",
                                                 .set      = 0,
                                                 .bindings = {
                                                     {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
                                                 },
                                             });

    _debugPPL = IPipelineLayout::create(
        _render, "FwdDebug_PPL", {PushConstantRange{.offset = 0, .size = sizeof(DebugModelPC), .stageFlags = EShaderStage::Vertex}}, {_debugDSL});

    _debugPipelineCI = GraphicsPipelineCreateInfo{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _debugPPL.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "Test/DebugRender.glsl",
            .vertexBufferDescs = {kAuxVBDesc},
            .vertexAttributes  = kAuxVertexAttributes3,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Scissor, EPipelineDynamicFeature::Viewport},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Back, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {{
                                   .index               = 0,
                                   .bBlendEnable        = false,
                                   .srcColorBlendFactor = EBlendFactor::One,
                                   .dstColorBlendFactor = EBlendFactor::Zero,
                                   .colorBlendOp        = EBlendOp::Add,
                                   .srcAlphaBlendFactor = EBlendFactor::One,
                                   .dstAlphaBlendFactor = EBlendFactor::Zero,
                                   .alphaBlendOp        = EBlendOp::Add,
                                   .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                               }}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _debugPipeline = IGraphicsPipeline::create(_render);
    _debugPipeline->recreate(_debugPipelineCI);

    _debugDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
        .maxSets   = 1,
        .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1}},
    });
    _debugUboDS = _debugDSP->allocateDescriptorSets(_debugDSL);
    _debugUboBuffer = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
        .label       = "FwdDebug_UBO",
        .usage       = EBufferUsage::UniformBuffer,
        .size        = sizeof(DebugUBO),
        .memoryUsage = EMemoryUsage::CpuToGpu,
    });
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genSingleBufferWrite(_debugUboDS, 0, EPipelineDescriptorType::UniformBuffer, _debugUboBuffer.get()),
    }, {});
}

void ForwardViewportAuxPasses::drawSkybox(const DrawContext& drawCtx)
{
    const auto& ctx = drawCtx.stageCtx;
    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0 || !drawCtx.skybox.bAvailable) return;

    cmdBuf->debugBeginLabel("ForwardSkybox");
    cmdBuf->bindPipeline(_skyboxPipeline.get());
    drawCtx.setViewportAndScissor(cmdBuf, vpW, vpH);
    cmdBuf->bindDescriptorSets(_skyboxPPL.get(), 0, {_skyboxFrameDS[ctx.flightIndex], drawCtx.skybox.descriptorSet});
    drawCtx.skybox.mesh->draw(cmdBuf);
    cmdBuf->debugEndLabel();
}

void ForwardViewportAuxPasses::drawSimple(const DrawContext& drawCtx)
{
    const auto& ctx          = drawCtx.stageCtx;
    const auto& fd           = *ctx.frameData;
    const auto& staticItems  = fd.drawBuckets.staticMeshes.simpleDrawItems;
    const auto& skinnedItems = fd.drawBuckets.skinnedMeshes.simpleDrawItems;
    auto*       cmdBuf       = ctx.cmdBuf;

    if (staticItems.empty() && skinnedItems.empty()) return;

    cmdBuf->debugBeginLabel("ForwardSimple");
    cmdBuf->bindPipeline(_simplePipeline.get());
    drawCtx.setViewportAndScissor(cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height);

    SimplePC pc{};
    pc.view       = fd.view;
    pc.projection = fd.projection;

    auto drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            auto* mat    = static_cast<SimpleMaterial*>(item.material);
            pc.model     = item.worldMatrix;
            pc.colorType = mat->colorType;
            cmdBuf->pushConstants(_simplePPL.get(), EShaderStage::Vertex, 0, sizeof(SimplePC), &pc);
            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };

    drawBucket(staticItems, false);
    drawBucket(skinnedItems, true);
    cmdBuf->debugEndLabel();
}

void ForwardViewportAuxPasses::drawDirectionOverlay(const DrawContext& drawCtx)
{
    auto* scene = drawCtx.activeScene;
    if (!scene) return;

    const auto& dirView = scene->getRegistry().view<TransformComponent, DirectionComponent>();
    if (dirView.begin() == dirView.end()) return;

    const auto& ctx = drawCtx.stageCtx;
    auto* cmdBuf = ctx.cmdBuf;
    cmdBuf->debugBeginLabel("ForwardDirectionOverlay");
    cmdBuf->bindPipeline(_simplePipeline.get());
    drawCtx.setViewportAndScissor(cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height);

    SimplePC pc{};
    pc.view       = ctx.frameData->view;
    pc.projection = ctx.frameData->projection;

    auto* cone     = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cone);
    auto* cylinder = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cylinder);

    glm::mat4 coneLocalTransf =
        glm::rotate(glm::mat4(1.0), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0), glm::vec3(0.3f, 1.0f, 0.3f));
    glm::mat4 cylinderLocalTransf =
        glm::rotate(glm::mat4(1.0), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0), glm::vec3(0.1f, 1.0f, 0.1f));

    pc.colorType = _simpleDefaultColorType;
    for (auto entity : dirView) {
        const auto& [tc, dc] = dirView.get(entity);

        glm::mat4 worldTransform = glm::translate(glm::mat4(1.0), tc.getWorldPosition()) *
                                   glm::mat4_cast(glm::quat(glm::radians(tc.getRotation())));

        pc.model = glm::translate(glm::mat4(1.0), -tc.getForward()) * coneLocalTransf * worldTransform;
        cmdBuf->pushConstants(_simplePPL.get(), EShaderStage::Vertex, 0, sizeof(SimplePC), &pc);
        cone->draw(cmdBuf);

        pc.model = worldTransform * cylinderLocalTransf;
        cmdBuf->pushConstants(_simplePPL.get(), EShaderStage::Vertex, 0, sizeof(SimplePC), &pc);
        cylinder->draw(cmdBuf);
    }

    cmdBuf->debugEndLabel();
}

void ForwardViewportAuxPasses::drawDebug(const DrawContext& drawCtx)
{
    if (_debugMode == DebugNone || !drawCtx.debugDraw.bHasDraws) return;

    const auto& ctx = drawCtx.stageCtx;
    auto*       cmdBuf = ctx.cmdBuf;
    const auto& fd = *ctx.frameData;
    auto vpW = ctx.viewportExtent.width;
    auto vpH = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) return;

    _debugUBO.projection = fd.projection;
    _debugUBO.view       = fd.view;
    _debugUBO.resolution = glm::ivec2(static_cast<int>(vpW), static_cast<int>(vpH));
    _debugUBO.time       = _getElapsedTimeSeconds ? static_cast<float>(_getElapsedTimeSeconds()) : 0.0f;
    _debugUboBuffer->writeData(&_debugUBO, sizeof(DebugUBO), 0);

    cmdBuf->debugBeginLabel("ForwardDebug");
    cmdBuf->bindPipeline(_debugPipeline.get());
    drawCtx.setViewportAndScissor(cmdBuf, vpW, vpH);

    auto drawItems = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh) continue;
            DebugModelPC pc{.modelMat = item.worldMatrix};
            cmdBuf->bindDescriptorSets(_debugPPL.get(), 0, {_debugUboDS});
            cmdBuf->pushConstants(_debugPPL.get(), EShaderStage::Vertex, 0, sizeof(DebugModelPC), &pc);
            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };
    for (uint32_t bucketIndex = 0; bucketIndex < drawCtx.debugDraw.count; ++bucketIndex) {
        const auto& bucket = drawCtx.debugDraw.buckets[bucketIndex];
        if (!bucket.items) {
            continue;
        }
        drawItems(*bucket.items, bucket.bSkinned);
    }

    cmdBuf->debugEndLabel();
}

void ForwardViewportAuxPasses::renderSettingsGUI()
{
    ImGui::Combo("Simple Color Type", &_simpleDefaultColorType, "Normal\0UV\0Fixed");
}

void ForwardViewportAuxPasses::renderDebugGUI()
{
    const char* modeNames[] = {"None", "NormalColor", "NormalDir", "Depth", "UV"};
    int         mode        = static_cast<int>(_debugMode);
    if (ImGui::Combo("Mode", &mode, modeNames, IM_ARRAYSIZE(modeNames))) {
        EDebugMode newMode = static_cast<EDebugMode>(mode);
        if (newMode != _debugMode) {
            if (newMode == DebugNormalDir) {
                _debugPipelineCI.shaderDesc.defines = {"DEBUG_NORMAL_DIR"};
                _debugPipeline->updateDesc(_debugPipelineCI);
            }
            else if (_debugMode == DebugNormalDir) {
                _debugPipelineCI.shaderDesc.defines = {};
                _debugPipeline->updateDesc(_debugPipelineCI);
            }
            _debugMode     = newMode;
            _debugUBO.mode = static_cast<int>(_debugMode);
        }
    }
    ImGui::DragFloat4("Float Param", glm::value_ptr(_debugUBO.floatParam), 0.1f);
}

void ForwardViewportAuxPasses::renderGUIPipelines()
{
    if (ImGui::TreeNode("Simple")) {
        _simplePipeline->renderGUI();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Skybox")) {
        _skyboxPipeline->renderGUI();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Debug Pipeline")) {
        _debugPipeline->renderGUI();
        ImGui::TreePop();
    }
}

} // namespace ya
