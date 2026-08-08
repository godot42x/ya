#include "ForwardViewportAuxPasses.h"

#include "Foundation/Core/Math/Math.h"
#include "Foundation/RHI/Core/Buffer.h"
#include "Foundation/RHI/Core/RenderResourceFactory.h"
#include "Foundation/RHI/Render.h"
#include "Framework/Game/Render/Render3D/Common/RenderViewportUtils.h"
#include "Framework/Game/Resource/Mesh/PrimitiveMeshCache.h"
#include "Framework/Game/Render/Render3D/Scene.h"
#include "Framework/Game/Render/Render3D/Stage/IRenderStage.h"

#include "glm/gtc/type_ptr.hpp"
#include <glm/gtc/matrix_transform.hpp>

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
    _render          = desc.render;
    _runtimeServices = desc.runtimeServices;
    _skyboxFrameDSL  = desc.skyboxFrameDSL;
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

    _debugPipeline.reset();
    _debugPPL.reset();
    _debugDSL.reset();
    _debugDSP.reset();
    _debugUboBuffer.reset();

    _runtimeServices = nullptr;
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

void ForwardViewportAuxPasses::setDebugMode(EDebugMode mode)
{
    if (mode == _debugMode) {
        return;
    }

    if (_debugPipeline) {
        if (mode == DebugNormalDir) {
            _debugPipelineCI.shaderDesc.defines = {"DEBUG_NORMAL_DIR"};
            _debugPipeline->updateDesc(_debugPipelineCI);
        }
        else if (_debugMode == DebugNormalDir) {
            _debugPipelineCI.shaderDesc.defines = {};
            _debugPipeline->updateDesc(_debugPipelineCI);
        }
    }

    _debugMode     = mode;
    _debugUBO.mode = static_cast<int>(_debugMode);
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

void ForwardViewportAuxPasses::prepare(const RenderStageContext& ctx,
                                       SkyboxFrameUBO& outFrame)
{
    if (!ctx.frameData) {
        return;
    }

    outFrame = SkyboxFrameUBO{
        .proj = ctx.frameData->projection,
        .view       = FMath::dropTranslation(ctx.frameData->view),
    };
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
    auto dsls = IDescriptorSetLayout::create(_render, std::vector<DescriptorSetLayoutDesc>{
        DescriptorSetLayoutDesc{
            .label    = "FwdSkybox_Resource_DSL",
            .set      = 1,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
        },
    });
    _skyboxResourceDSL = dsls[0];

    auto pipelineDsls = dsls;
    pipelineDsls.insert(pipelineDsls.begin(), _skyboxFrameDSL);
    _skyboxPPL = IPipelineLayout::create(_render, "FwdSkybox_PPL", {}, pipelineDsls);

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

}

void ForwardViewportAuxPasses::initDebug(const InitDesc& desc)
{
    if (!_render->supportsGeometryShader()) {
        _debugMode = DebugNone;
        return;
    }

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
    setViewportAndScissor(*cmdBuf, vpW, vpH, drawCtx.bReverseViewportY);
    cmdBuf->bindDescriptorSets(_skyboxPPL.get(), 0, {drawCtx.skyboxFrameDescriptorSet, drawCtx.skybox.descriptorSet});
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
    setViewportAndScissor(*cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height, drawCtx.bReverseViewportY);

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
    if (drawCtx.directionGizmos.empty()) return;

    const auto& ctx = drawCtx.stageCtx;
    auto* cmdBuf = ctx.cmdBuf;
    cmdBuf->debugBeginLabel("ForwardDirectionOverlay");
    cmdBuf->bindPipeline(_simplePipeline.get());
    setViewportAndScissor(*cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height, drawCtx.bReverseViewportY);

    SimplePC pc{};
    pc.view       = ctx.frameData->view;
    pc.projection = ctx.frameData->projection;

    auto* cone     = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cone);
    auto* cylinder = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cylinder);

    pc.colorType = _simpleDefaultColorType;
    for (const auto& gizmo : drawCtx.directionGizmos) {
        pc.model = gizmo.coneModel;
        cmdBuf->pushConstants(_simplePPL.get(), EShaderStage::Vertex, 0, sizeof(SimplePC), &pc);
        cone->draw(cmdBuf);

        pc.model = gizmo.cylinderModel;
        cmdBuf->pushConstants(_simplePPL.get(), EShaderStage::Vertex, 0, sizeof(SimplePC), &pc);
        cylinder->draw(cmdBuf);
    }

    cmdBuf->debugEndLabel();
}

void ForwardViewportAuxPasses::drawDebug(const DrawContext& drawCtx)
{
    if (_debugMode == DebugNone || !_debugPipeline || !_debugUboBuffer || !drawCtx.debugDraw.bHasDraws) return;

    const auto& ctx = drawCtx.stageCtx;
    auto*       cmdBuf = ctx.cmdBuf;
    const auto& fd = *ctx.frameData;
    auto vpW = ctx.viewportExtent.width;
    auto vpH = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) return;

    _debugUBO.projection = fd.projection;
    _debugUBO.view       = fd.view;
    _debugUBO.resolution = glm::ivec2(static_cast<int>(vpW), static_cast<int>(vpH));
    _debugUBO.time       = _runtimeServices ? static_cast<float>(_runtimeServices->getElapsedTimeSeconds()) : 0.0f;
    _debugUboBuffer->writeData(&_debugUBO, sizeof(DebugUBO), 0);

    cmdBuf->debugBeginLabel("ForwardDebug");
    cmdBuf->bindPipeline(_debugPipeline.get());
    setViewportAndScissor(*cmdBuf, vpW, vpH, drawCtx.bReverseViewportY);

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

} // namespace ya
