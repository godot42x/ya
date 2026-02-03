#pragma once
#include "Core/Base.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Sampler.h"



namespace ya
{



struct BasicPostprocessing
{
    YA_REFLECT_BEGIN(BasicPostprocessing)
    YA_REFLECT_END()

    struct PostProcessingVertex
    {
        glm::vec3 position;
        glm::vec2 texCoord0;
    };

    struct PushConstant
    {
        uint32_t effect;
    };

    stdptr<IPipelineLayout>   _pipelineLayout;
    stdptr<IGraphicsPipeline> _pipeline;

    stdptr<IDescriptorSetLayout>     _dslInputTexture;
    std::shared_ptr<IDescriptorPool> _descriptorPool;
    DescriptorSetHandle              _descriptorSet;

    ya::Ptr<Sampler> _sampler;

    // Current bound input image view (for descriptor update check)
    ImageViewHandle _currentInputImageViewHandle = nullptr;

    ~BasicPostprocessing()
    {
        _descriptorPool.reset();
        _pipeline.reset();
        _dslInputTexture.reset();
        _pipelineLayout.reset();
        _sampler.reset();
    }

    void init(const DynamicRenderingInfo *dynamicRenderingInfo = nullptr);
    void update() {}

    enum EEffect
    {
        Inversion = 0,
        Grayscale = 1
    };

    struct RenderPayload
    {
        IImageView *inputImageView;
        Extent2D    extent;
        EEffect     effect;
    };
    void render(ICommandBuffer *cmdBuf, const RenderPayload &payload);

    void reloadShader()
    {
        _pipeline->reloadShaders();
    }
};

}; // namespace ya