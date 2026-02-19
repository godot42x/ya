#include "BasicPostprocessing.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Sampler.h"
#include "Render/Render.h"
#include "Resource/TextureLibrary.h"
#include "utility.cc/ranges.h"
#include <algorithm>


#include "imgui.h"

namespace ya

{

void BasicPostprocessing::onInitImpl(const InitParams& initParams)
{
    auto render = getRender();



    // MARK: layout

    auto DSLs        = IDescriptorSetLayout::create(render, _pipelineLayoutDesc.descriptorSetLayouts);
    _dslInputTexture = DSLs[0];

    _pipelineLayout = IPipelineLayout::create(
        render,
        _pipelineLayoutDesc.label,
        _pipelineLayoutDesc.pushConstants,
        DSLs);

    auto _pipelineDesc = GraphicsPipelineCreateInfo{
        .renderPass            = initParams.renderPass,
        .pipelineRenderingInfo = initParams.pipelineRenderingInfo,
        .pipelineLayout        = _pipelineLayout.get(),
        .shaderDesc            = ShaderDesc{
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
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = false,
            .bDepthWriteEnable      = false,
            .depthCompareOp         = ECompareOp::Always,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
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
            .viewports = {Viewport::defaults()},
            .scissors  = {Scissor::defaults()},
        },
    };
    _pipeline = IGraphicsPipeline::create(render);
    _pipeline->recreate(_pipelineDesc);

    // MARK: sampler

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

void BasicPostprocessing::onRender(ICommandBuffer* cmdBuf, const FrameContext* /*ctx*/)
{
    if (!_inputImageView || _renderExtent.width == 0 || _renderExtent.height == 0) {
        return;
    }

    auto render = getRender();

    auto imageViewHandle = _inputImageView->getHandle();
    // Update descriptor set only if input image view changed
    if (_currentInputImageViewHandle != imageViewHandle) {
        _currentInputImageViewHandle = imageViewHandle;

        static auto sampler = TextureLibrary::get().getDefaultSampler();

        DescriptorImageInfo imageInfo(sampler->getHandle(),
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
    const auto& extent = _renderExtent;
    cmdBuf->setViewport(0, 0, (float)extent.width, (float)extent.height);
    cmdBuf->setScissor(0, 0, extent.width, extent.height);

    // Bind descriptor set
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(),
                               0,
                               {_descriptorSet},
                               {});

    // Push constants - two separate ranges
    const auto& pcs = _pipelineLayoutDesc.pushConstants;

    pc.effect = static_cast<uint32_t>(effect);
    pc.gamma  = _bOutputIsSRGB ? 1.0f : std::max(pc.gamma, 0.001f);
    pc.floatParams = floatParams;
    cmdBuf->pushConstants(_pipelineLayout.get(),
                          pcs[0].stageFlags,
                          pcs[0].offset,
                          pcs[0].size,
                          &pc);

    // FragPushConstant fragPC{
    //     .floatParams = payload.floatParams,
    // };
    // cmdBuf->pushConstants(_pipelineLayout.get(),
    //                       pcs[1].stageFlags,
    //                       pcs[1].offset,
    //                       pcs[1].size,
    //                       &fragPC);

    cmdBuf->draw(6, 1, 0, 0);
}

void BasicPostprocessing::onRenderGUI()
{
    Super::onRenderGUI();
    ImGui::Combo("Effect",
                 reinterpret_cast<int*>(&effect),
                 "None\0Inversion\0Grayscale\0Weighted Grayscale\0"
                 "Kernel_Sharpe\0Kernel_Blur\0Kernel_Edge-Detection\0Tone Mapping\0"
                 "Random\0");
    ImGui::BeginDisabled(_bOutputIsSRGB);
    ImGui::DragFloat("Gamma", &pc.gamma, 0.01f, 0.1f, 10.0f);
    ImGui::EndDisabled();
    for (const auto& [i, p] : ut::enumerate(floatParams)) {
        ImGui::DragFloat4(std::format("{}", i).c_str(), &p.x);
    }
}

} // namespace ya
