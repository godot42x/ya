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

    struct FragPushConstant
    {
        std::array<glm::vec4, 4> floatParams;
    } fragPC;


    enum EEffect
    {
        Inversion           = 0,
        Grayscale           = 1,
        WeightedGrayscale   = 2,
        KernalSharpen       = 3,
        KernalBlur          = 4,
        KernalEdgeDetection = 5,
        ToneMapping         = 6,
        Random              = 7, // shader do nothing, 老电视机花屏效果
    };

    struct RenderPayload
    {
        IImageView               *inputImageView;
        Extent2D                  extent;
        EEffect                   effect;
        std::array<glm::vec4, 4> &floatParams;
    };

    PipelineLayoutDesc _pipelineLayoutDesc{
        .label         = "InversionSystem_PipelineLayout",
        .pushConstants = {
            // Vertex stage: offset 0
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(BasicPostprocessing::PushConstant),
                .stageFlags = EShaderStage::Vertex,
            },
            // Fragment stage: offset after vertex PC
            PushConstantRange{
                .offset     = nextAligned(sizeof(BasicPostprocessing::PushConstant), 16),
                .size       = sizeof(BasicPostprocessing::FragPushConstant),
                .stageFlags = EShaderStage::Fragment,
            },
        },
        .descriptorSetLayouts = {
            DescriptorSetLayout{
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

    void init();
    void update() {}

    void render(ICommandBuffer *cmdBuf, const RenderPayload &payload);

    void reloadShader()
    {
        _pipeline->reloadShaders();
    }
};

}; // namespace ya