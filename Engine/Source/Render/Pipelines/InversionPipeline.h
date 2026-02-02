#pragma once
#include "Core/Base.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Sampler.h"




namespace ya
{



struct InversionPipeline
{
    YA_REFLECT_BEGIN(InversionPipeline)
    YA_REFLECT_END()

    struct PostProcessingVertex
    {
        glm::vec3 position;
        glm::vec2 texCoord0;
    };

    stdptr<IPipelineLayout>   _pipelineLayout;
    stdptr<IGraphicsPipeline> _pipeline;

    stdptr<IDescriptorSetLayout>     _dslInputTexture;
    std::shared_ptr<IDescriptorPool> _descriptorPool;
    DescriptorSetHandle              _descriptorSet;

    ya::Ptr<Sampler> _sampler;

    // Current bound input image view (for descriptor update check)
    ImageViewHandle _currentInputImageViewHandle = nullptr;

    ~InversionPipeline()
    {
        _descriptorPool.reset();
        _pipeline.reset();
        _dslInputTexture.reset();
        _pipelineLayout.reset();
        _sampler.reset();
    }

    void init(const DynamicRenderingInfo *dynamicRenderingInfo = nullptr);
    void update() {}

    /**
     * @brief Render with specified input and output
     * @param cmdBuf Command buffer
     * @param inputImageView Input texture to sample (e.g., _viewportRT color attachment)
     * @param outputImageView Output image view to render to (used for viewport/scissor setup)
     * @param outputExtent Extent of the output image
     */
    void render(ICommandBuffer *cmdBuf, IImageView *inputImageView, const Extent2D &outputExtent);

    void reloadShader()
    {
        _pipeline->reloadShaders();
    }
};

}; // namespace ya