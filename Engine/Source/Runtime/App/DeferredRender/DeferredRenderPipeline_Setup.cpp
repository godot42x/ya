#include "DeferredRenderPipeline.h"

#include "Resource/TextureLibrary.h"

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════
// Render Targets
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::initRenderTargets(Extent2D extent)
{
    _gBufferRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "GBuffer RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = extent,
        .frameBufferCount = 1,
        .attachments      = {

            .colorAttach = {
                AttachmentDescription{
                    .index         = 0,
                    .format        = SIGNED_LINEAR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 1,
                    .format        = SIGNED_LINEAR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 2,
                    .format        = LINEAR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 3,
                    .format        = SHADING_MODEL_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
            },
            .depthAttach = AttachmentDescription{
                .index          = 4,
                .format         = DEPTH_FORMAT,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::Clear,
                .stencilStoreOp = EAttachmentStoreOp::Store,
                .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                .usage          = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
            },
        },
    });

    _viewportRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "Deferred Viewport RT",
        .bSwapChainTarget = false,
        .extent           = extent,
        .attachments      = {
                 .colorAttach = {AttachmentDescription{
                     .index          = 0,
                     .format         = LINEAR_FORMAT,
                     .samples        = ESampleCount::Sample_1,
                     .loadOp         = EAttachmentLoadOp::Clear,
                     .storeOp        = EAttachmentStoreOp::Store,
                     .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                     .stencilStoreOp = EAttachmentStoreOp::DontCare,
                     .initialLayout  = EImageLayout::ColorAttachmentOptimal,
                     .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                     .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            }},
        },
    });
}

// ═══════════════════════════════════════════════════════════════════════
// Shared Resources (Frame+Light DSL)
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::initSharedResources()
{
    _frameAndLightDSL = IDescriptorSetLayout::create(
        _render,
        {DescriptorSetLayoutDesc{
            .label    = "Deferred_Frame_And_Light_DSL",
            .set      = 0,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::All},
            },
        }});
}

// ═══════════════════════════════════════════════════════════════════════
// Light Pass Pipeline
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::initLightPassPipeline()
{
    // GBuffer DSL (set 1: 4 GBuffer textures)
    _lightGBufferDSL = IDescriptorSetLayout::create(
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

    _lightPPL = IPipelineLayout::create(
        _render, "Deferred_Light_PPL", {PushConstantRange{.offset = 0, .size = sizeof(LightPassPushConstant), .stageFlags = EShaderStage::Vertex}}, {_frameAndLightDSL, _lightGBufferDSL});

    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {
            .label                  = "Deferred Light Pass",
            .colorAttachmentFormats = {LINEAR_FORMAT},
            .depthAttachmentFormat  = DEPTH_FORMAT,
        },
        .pipelineLayout = _lightPPL.get(),
        .shaderDesc     = ShaderDesc{
                .shaderName        = "DeferredRender/Unified_LightPass.slang",
                .bDeriveFromShader = false,
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
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };

    _lightPipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_lightPipeline && _lightPipeline->recreate(ci), "Failed to create Light pipeline");
}

// ═══════════════════════════════════════════════════════════════════════
// Shared Descriptors + UBOs
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::initDescriptorsAndUBOs()
{
    _deferredDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "Deferred_DSP",
            .maxSets   = 2,
            .poolSizes = {
                {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 2},
                {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 4},
            },
        });

    _frameAndLightDS = _deferredDSP->allocateDescriptorSets(_frameAndLightDSL);
    _lightTexturesDS = _deferredDSP->allocateDescriptorSets(_lightGBufferDSL);

    _frameUBO = IBuffer::create(
        _render,
        BufferCreateInfo{
            .label         = "Deferred_Frame_UBO",
            .usage         = EBufferUsage::UniformBuffer,
            .size          = sizeof(PBRGBufferFrameData),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
    _lightUBO = IBuffer::create(
        _render,
        BufferCreateInfo{
            .label         = "Deferred_Light_UBO",
            .usage         = EBufferUsage::UniformBuffer,
            .size          = sizeof(LightPassLightData),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    _frameUBO->writeData(&_gBufferPassFrameData, sizeof(_gBufferPassFrameData), 0);
    _frameUBO->flush();
    _lightUBO->writeData(&_lightPassLightData, sizeof(_lightPassLightData), 0);
    _lightUBO->flush();

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneUniformBuffer(_frameAndLightDS, 0, _frameUBO.get()),
        IDescriptorSetHelper::writeOneUniformBuffer(_frameAndLightDS, 1, _lightUBO.get()),
    });
}

} // namespace ya
