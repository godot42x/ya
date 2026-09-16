#pragma once

#include "Core/Base.h"
#include "Misc.BasicPostprocessing.slang.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "Render3D/Common/PostProcessingState.h"
#include "Render3D/Common/ViewDescriptorSetAllocator.h"

#include <unordered_map>

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
        uint64_t                   viewId         = 0;
    };

    using PushConstants = slang_types::Misc::BasicPostprocessing::PushConstants;

    IRender*                         _render                      = nullptr;
    InitDesc                         _initDesc                    = {};
    PushConstants                    _pushConstants               = {};
    stdptr<IPipelineLayout>          _pipelineLayout;
    stdptr<IGraphicsPipeline>        _pipeline;
    stdptr<IDescriptorSetLayout>     _dslInputTexture;
    ViewDescriptorSetAllocator       _viewSets;
    std::unordered_map<uint64_t, std::pair<DescriptorSetHandle, ImageViewHandle>> _viewBindings;

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
  private:
    void rebuildPushConstants(const PostProcessingState& state, bool bOutputIsSRGB);
    DescriptorSetHandle bindViewInput(uint64_t viewId, IImageView* inputImageView);
};

} // namespace ya
