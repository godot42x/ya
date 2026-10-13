#include "Render3D/Deferred/SkyboxStage.h"

#include "Core/Math/Math.h"
#include "Core/Profiling/Instrumentor.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "Render3D/RenderFrameData.h"

#include <glm/glm.hpp>

namespace ya
{

void SkyboxStage::init(IRender* render)
{
    _render = render;

    _frameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_Skybox_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });
    YA_CORE_ASSERT(_frameDSL != nullptr, "SkyboxStage requires a frame descriptor layout");
    _resourceDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "SkyboxOverlay_Resource_DSL",
            .set      = 1,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
        });

    _pipelineLayout = IPipelineLayout::create(_render, "SkyboxOverlay_PPL", {}, {_frameDSL, _resourceDSL});

    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {
            .label                  = "Deferred Skybox Overlay",
            .colorAttachmentFormats = {LINEAR_FORMAT},
            .depthAttachmentFormat  = DEPTH_FORMAT,
        },
        .pipelineLayout = _pipelineLayout.get(),
        .shaderDesc     = ShaderDesc{
            .shaderName        = "Skybox.slang",
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
    _pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_pipeline && _pipeline->recreate(ci), "Failed to create Skybox overlay pipeline");
}

void SkyboxStage::destroy()
{
    _pipeline.reset();
    _pipelineLayout.reset();
    _frameDSL.reset();
    _resourceDSL.reset();
    _render = nullptr;
}

void SkyboxStage::beginFrame()
{
    YA_PROFILE_FUNCTION();
    if (_pipeline) {
        _pipeline->beginFrame();
    }
}

void SkyboxStage::refreshPipelineFormats(const DeferredAttachmentFormats& formats)
{
    if (!_pipeline || !formats.hasColor()) {
        return;
    }
    auto ci                                         = _pipeline->getDesc();
    ci.pipelineRenderingInfo.colorAttachmentFormats = {formats.colorFormats.front()};
    ci.pipelineRenderingInfo.depthAttachmentFormat  = formats.depthFormat.value_or(EFormat::Undefined);
    _pipeline->updateDesc(std::move(ci));
}

void SkyboxStage::execute(const RenderStageContext& ctx)
{
    (void)ctx;
    // Skybox is recorded exclusively by SkyboxFeature's graph pass.
}

SkyboxStage::FrameUBO SkyboxStage::buildFrameData(const RenderFrameData& frameData)
{
    return FrameUBO{
        .proj = frameData.projection,
        .view = FMath::dropTranslation(frameData.view),
    };
}

void SkyboxStage::executeSkybox(const RenderStageContext& ctx,
                                const ViewOverlayFrameInputs::SkyboxInput& skyboxInput,
                                DescriptorSetHandle frameDescriptorSet)
{
    if (!ctx.cmdBuf || !ctx.frameData) {
        return;
    }
    drawSkybox(ctx, skyboxInput, frameDescriptorSet);
}

void SkyboxStage::drawSkybox(const RenderStageContext& ctx,
                             const ViewOverlayFrameInputs::SkyboxInput& skyboxInput,
                             DescriptorSetHandle frameDescriptorSet)
{
    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewExtent.width;
    auto  vpH    = ctx.viewExtent.height;
    if (vpW == 0 || vpH == 0) return;

    // Check if skybox is available
    if (!skyboxInput.bAvailable || !skyboxInput.mesh || !frameDescriptorSet) return;

    cmdBuf->debugBeginLabel("Skybox");

    cmdBuf->bindPipeline(_pipeline.get());

    float viewportY  = 0.0f;
    float viewHeight = static_cast<float>(vpH);
    if (bReverseViewportY) {
        viewportY  = static_cast<float>(vpH);
        viewHeight = -static_cast<float>(vpH);
    }
    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(vpW), viewHeight, 0.0f, 1.0f);
    cmdBuf->setScissor(0, 0, vpW, vpH);

    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {frameDescriptorSet, skyboxInput.descriptorSet});
    skyboxInput.mesh->draw(cmdBuf);

    cmdBuf->debugEndLabel();
}

} // namespace ya
