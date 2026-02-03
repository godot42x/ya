#include "BasicPostprocessing.h"
#include "Core/App/App.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Sampler.h"
#include "Resource/TextureLibrary.h"

namespace ya

{

void BasicPostprocessing::init(const DynamicRenderingInfo *dynamicRenderingInfo)
{
    auto app        = App::get();
    auto render     = app->getRender();
    auto viewPortRT = app->_viewportRT;



    // MARK: layout

    auto DSLs        = IDescriptorSetLayout::create(render, _pipelineLayoutDesc.descriptorSetLayouts);
    _dslInputTexture = DSLs[0];

    _pipelineLayout = IPipelineLayout::create(
        render,
        _pipelineLayoutDesc.label,
        _pipelineLayoutDesc.pushConstants,
        DSLs);

    auto _pipelineDesc = GraphicsPipelineCreateInfo{
        .renderingMode         = ERenderingMode::DynamicRendering,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = "BasicPostprocessing",
            .colorAttachmentFormats = {
                EFormat::R8G8B8A8_UNORM,
            },
        },
        .shaderDesc = ShaderDesc{
            .shaderName        = "PostProcessing/Basic.glsl",
            .bDeriveFromShader = false,
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(BasicPostprocessing::PostProcessingVertex),
                },
            },
            .vertexAttributes = {
                // (location=0) in vec3 aPos,
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 0,
                    .format     = EVertexAttributeFormat::Float3,
                    .offset     = offsetof(BasicPostprocessing::PostProcessingVertex, position),
                },
                //  texcoord
                VertexAttribute{
                    .bufferSlot = 0, // same buffer slot
                    .location   = 1,
                    .format     = EVertexAttributeFormat::Float2,
                    .offset     = offsetof(BasicPostprocessing::PostProcessingVertex, texCoord0),
                },
            },
        },
        // define what state need to dynamically modified in render pass execution
        .dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
        },
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Back,
            .frontFace = EFrontFaceType::CounterClockWise, // GL
        },
        .colorBlendState = ColorBlendState{
            .attachments = {
                ColorBlendAttachmentState{
                    .index        = 0,
                    .bBlendEnable = false,
                },
            },
        },
        .viewportState = ViewportState{
            .viewports = {
                {
                    .x        = 0,
                    .y        = 0,
                    .width    = static_cast<float>(viewPortRT->getExtent().width),
                    .height   = static_cast<float>(viewPortRT->getExtent().height),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                },
            },
            .scissors = {Scissor{
                .offsetX = 0,
                .offsetY = 0,
                .width   = viewPortRT->getExtent().width,
                .height  = viewPortRT->getExtent().height,
            }},
        },
    };
    _pipeline = IGraphicsPipeline::create(render, _pipelineLayout.get());
    _pipeline->recreate(_pipelineDesc);

    // MARK: sampler
    _sampler = TextureLibrary::get().getDefaultSampler();

    // MARK: descriptor pool
    DescriptorPoolCreateInfo poolCI{
        .label     = "InversionPool",
        .maxSets   = 1,
        .poolSizes = {
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::CombinedImageSampler,
                .descriptorCount = 1,
            },
        },
    };
    _descriptorPool = IDescriptorPool::create(render, poolCI);

    // MARK: allocate descriptor set
    std::vector<DescriptorSetHandle> descriptorSets;

    bool ok = _descriptorPool->allocateDescriptorSets(_dslInputTexture, 1, descriptorSets);
    YA_CORE_ASSERT(ok, "Failed to allocate descriptor set");
    _descriptorSet = descriptorSets[0];
}

void BasicPostprocessing::render(ICommandBuffer *cmdBuf, const RenderPayload &payload)
{

    auto app    = App::get();
    auto render = app->getRender();

    auto imageViewHandle = payload.inputImageView->getHandle();
    // Update descriptor set only if input image view changed
    if (_currentInputImageViewHandle != imageViewHandle) {
        _currentInputImageViewHandle = imageViewHandle;

        DescriptorImageInfo imageInfo(_sampler->getHandle(),
                                      _currentInputImageViewHandle,
                                      EImageLayout::ShaderReadOnlyOptimal);

        render->getDescriptorHelper()->updateDescriptorSets(
            {
                IDescriptorSetHelper::genImageWrite(
                    _descriptorSet,
                    0,
                    0,
                    EPipelineDescriptorType::CombinedImageSampler,
                    {imageInfo}),
            },
            {});
    }

    cmdBuf->bindPipeline(_pipeline.get());
    const auto &extent = payload.extent;
    cmdBuf->setViewport(0, 0, (float)extent.width, (float)extent.height);
    cmdBuf->setScissor(0, 0, extent.width, extent.height);

    // Bind descriptor set
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(),
                               0,
                               {_descriptorSet},
                               {});

    // Push constants - two separate ranges
    const auto &pr = _pipelineLayoutDesc.pushConstants;

    PushConstant pc{
        .effect = static_cast<uint32_t>(payload.effect),
    };
    cmdBuf->pushConstants(_pipelineLayout.get(),
                          pr[0].stageFlags,
                          pr[0].offset,
                          pr[0].size,
                          &pc);

    FragPushConstant fragPC{
        .floatParams = payload.floatParams,
    };
    cmdBuf->pushConstants(_pipelineLayout.get(),
                          pr[1].stageFlags,
                          pr[1].offset,
                          pr[1].size,
                          &fragPC);

    cmdBuf->draw(6, 1, 0, 0);
}

} // namespace ya
