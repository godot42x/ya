#pragma once

#include "Core/Base.h"
#include "Misc.BasicPostprocessing.slang.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Runtime/App/Common/PostProcessingState.h"

namespace ya
{

struct BasicPostprocessing
{
    struct InitDesc
    {
        IRender*              render                = nullptr;
        IRenderPass*          renderPass            = nullptr;
        PipelineRenderingInfo pipelineRenderingInfo = {};
    };

    struct RenderDesc
    {
        ICommandBuffer*            cmdBuf         = nullptr;
        const FrameContext*        ctx            = nullptr;
        IImageView*                inputImageView = nullptr;
        Extent2D                   renderExtent   = {.width = 0, .height = 0};
        bool                       bOutputIsSRGB  = false;
        const PostProcessingState* state          = nullptr;
    };

    using PushConstants = slang_types::Misc::BasicPostprocessing::PushConstants;

    IRender*                         _render                      = nullptr;
    InitDesc                         _initDesc                    = {};
    PushConstants                    _pushConstants               = {};
    stdptr<IPipelineLayout>          _pipelineLayout;
    stdptr<IGraphicsPipeline>        _pipeline;
    stdptr<IDescriptorSetLayout>     _dslInputTexture;
    std::shared_ptr<IDescriptorPool> _descriptorPool;
    DescriptorSetHandle              _descriptorSet               = nullptr;
    ImageViewHandle                  _currentInputImageViewHandle = nullptr;

    PipelineLayoutDesc _pipelineLayoutDesc{
        .label         = "BasicPostprocessing_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(PushConstants),
                .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment,
            },
        },
        .descriptorSetLayouts = {
            DescriptorSetLayoutDesc{
                .label    = "BasicPostprocessing_DSL",
                .set      = 0,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
        },
    };

    void init(const InitDesc& initDesc);
    void shutdown();
    void beginFrame();
    void render(const RenderDesc& desc);
    void renderGUI(PostProcessingState& state);

  private:
    void rebuildPushConstants(const PostProcessingState& state, bool bOutputIsSRGB);
    void reloadShader();
};

} // namespace ya