#pragma once
#include "Core/Base.h"
#include "ECS/System/Render/IRenderSystem.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Sampler.h"



namespace ya
{



struct BasicPostprocessing : public IRenderSystem
{
    YA_REFLECT_BEGIN(BasicPostprocessing, IRenderSystem)
    YA_REFLECT_END()

    struct PostProcessingVertex
    {
        glm::vec3 position;
        glm::vec2 texCoord0;
    };

    struct PushConstant
    {
        uint32_t                 effect;
        float                    gamma      = 2.2f; // gamma correction value for tone mapping, default to 2.2
        float                    padding[2] = {};   // Padding to make the size a multiple of 16 bytes (Vulkan requirement)
        std::array<glm::vec4, 4> floatParams;
    } pc;



    enum EEffect
    {
        None                = 0,
        Inversion           = 1,
        Grayscale           = 2,
        WeightedGrayscale   = 3,
        KernalSharpen       = 4,
        KernalBlur          = 5,
        KernalEdgeDetection = 6,
        ToneMapping         = 7,
        Random              = 8, // shader do nothing, 老电视机花屏效果
    };

    EEffect                  effect      = EEffect::None;
    std::array<glm::vec4, 4> floatParams = []() {
        std::array<glm::vec4, 4> params{};
        params[0].x = 1.0f / 300.0f; // kernel_sharpen defaults
        return params;
    }();

    IImageView* _inputImageView = nullptr;
    Extent2D    _renderExtent   = {.width = 0, .height = 0};
    bool        _bOutputIsSRGB  = false;

    PipelineLayoutDesc _pipelineLayoutDesc{
        .label         = "InversionSystem_PipelineLayout",
        .pushConstants = {
            // Vertex stage: offset 0
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(BasicPostprocessing::PushConstant),
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

    stdptr<IPipelineLayout> _pipelineLayout;
    // stdptr<IGraphicsPipeline> _pipeline;

    stdptr<IDescriptorSetLayout>     _dslInputTexture;
    std::shared_ptr<IDescriptorPool> _descriptorPool;
    DescriptorSetHandle              _descriptorSet;


    // Current bound input image view (for descriptor update check)
    ImageViewHandle _currentInputImageViewHandle = nullptr;

    BasicPostprocessing() : IRenderSystem("BasicPostprocessingSystem") {}

    ~BasicPostprocessing()
    {
        _descriptorPool.reset();
        // _pipeline.reset();
        _dslInputTexture.reset();
        _pipelineLayout.reset();
    }

    void setInputTexture(IImageView* imageView, const Extent2D& extent)
    {
        _inputImageView = imageView;
        _renderExtent   = extent;
    }

    void setOutputColorSpace(bool bOutputIsSRGB)
    {
        _bOutputIsSRGB = bOutputIsSRGB;
    }

    void onInitImpl(const InitParams& initParams) override;
    void onRender(ICommandBuffer* cmdBuf, const FrameContext* ctx) override;
    void onDestroy() override {}

  protected:
    void onRenderGUI() override;

    void reloadShader()
    {
        _pipeline->markDirty();
    }
};

}; // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::BasicPostprocessing::EEffect)
YA_REFLECT_ENUM_VALUE(None)
YA_REFLECT_ENUM_VALUE(Inversion)
YA_REFLECT_ENUM_VALUE(Grayscale)
YA_REFLECT_ENUM_VALUE(WeightedGrayscale)
YA_REFLECT_ENUM_VALUE(KernalSharpen)
YA_REFLECT_ENUM_VALUE(KernalBlur)
YA_REFLECT_ENUM_VALUE(KernalEdgeDetection)
YA_REFLECT_ENUM_VALUE(ToneMapping)
YA_REFLECT_ENUM_VALUE(Random)
YA_REFLECT_ENUM_END()
