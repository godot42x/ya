#include "LightStage.h"

#include "GBufferStage.h"
#include "Resource/PrimitiveMeshCache.h"
#include "Resource/TextureLibrary.h"
#include "Runtime/App/App.h"
#include "Runtime/App/RenderRuntime.h"

namespace ya
{

void LightStage::setup(GBufferStage* gBufferStage, IRenderTarget* gBufferRT)
{
    _gBufferStage = gBufferStage;
    _gBufferRT    = gBufferRT;
}

void LightStage::init(IRender* render)
{
    _render = render;
    YA_CORE_ASSERT(_gBufferStage, "LightStage requires GBufferStage (call setup() before init())");

    // GBuffer texture DSL (set 1)
    _gBufferTextureDSL = IDescriptorSetLayout::create(
        _render,
        {DescriptorSetLayoutDesc{
            .label    = "Deferred_LightPass_GBuffer_DSL",
            .set      = 1,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
            },
        }});

    // Pipeline layout: set 0 = frame+light (from GBufferStage), set 1 = GBuffer textures, set 2 = environment lighting
    auto* runtime = App::get()->getRenderRuntime();
    _pipelineLayout = IPipelineLayout::create(
        _render,
        "Deferred_Light_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(PushConstant), .stageFlags = EShaderStage::Vertex}},
        {
            _gBufferStage->getFrameAndLightDSL(),
            _gBufferTextureDSL,
            runtime->getEnvironmentLightingDescriptorSetLayout(),
        });

    // Pipeline
    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {
            .label                  = "Deferred Light Pass",
            .colorAttachmentFormats = {LINEAR_FORMAT},
            .depthAttachmentFormat  = DEPTH_FORMAT,
        },
        .pipelineLayout = _pipelineLayout.get(),
        .shaderDesc     = ShaderDesc{
            .shaderName        = "DeferredRender/Unified_LightPass.slang",
            .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
            .vertexAttributes  = _commonVertexAttributes,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.cullMode = ECullMode::None, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = false, .bDepthWriteEnable = false},
        .colorBlendState    = {.attachments = {ColorBlendAttachmentState{
            .index          = 0,
            .bBlendEnable   = false,
            .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
        }}},
        .viewportState = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_pipeline && _pipeline->recreate(ci), "Failed to create Light pipeline");

    // Descriptor pool for GBuffer texture DS
    _dsp = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
        .label     = "LightStage_GBuffer_DSP",
        .maxSets   = 1,
        .poolSizes = {
            {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 4},
        },
    });
    _gBufferTextureDS = _dsp->allocateDescriptorSets(_gBufferTextureDSL);
}

void LightStage::destroy()
{
    _pipeline.reset();
    _pipelineLayout.reset();
    _gBufferTextureDSL.reset();
    _dsp.reset();
    _render       = nullptr;
    _gBufferStage = nullptr;
    _gBufferRT    = nullptr;
}

void LightStage::prepare(const RenderStageContext& ctx)
{
    if (!_gBufferRT) return;

    // Update GBuffer texture DS from current GBuffer RT frame buffer
    auto  sampler = TextureLibrary::get().getDefaultSampler();
    auto* fb      = _gBufferRT->getCurFrameBuffer();
    if (!fb) return;

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 0, fb->getColorTexture(0)->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 1, fb->getColorTexture(1)->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 2, fb->getColorTexture(2)->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 3, fb->getColorTexture(3)->getImageView(), sampler.get()),
    });
}

void LightStage::execute(const RenderStageContext& ctx)
{
    if (!ctx.cmdBuf || !_gBufferStage) return;

    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;

    // Get environment lighting DS from RenderRuntime
    auto* runtime             = App::get()->getRenderRuntime();
    auto  environmentLightingDS = runtime ? runtime->getSceneEnvironmentLightingDescriptorSet() : nullptr;

    cmdBuf->debugBeginLabel("LightStage");

    cmdBuf->bindPipeline(_pipeline.get());
    cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(vpW), static_cast<float>(vpH));
    cmdBuf->setScissor(0, 0, vpW, vpH);

    // set 0 = frame+light (from GBufferStage), set 1 = GBuffer textures, set 2 = environment
    auto frameAndLightDS = _gBufferStage->getFrameAndLightDS(ctx.flightIndex);
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {
        frameAndLightDS,
        _gBufferTextureDS,
        environmentLightingDS,
    });

    PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Quad)->draw(cmdBuf);

    cmdBuf->debugEndLabel();
}

} // namespace ya
