

#pragma once

#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Image.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/Core/RenderImage.h"
#include "RHI/Core/Sampler.h"
#include "RHI/Core/Texture.h"

#include "Misc.CubeMap2PBRPrefilterEnv.slang.h"

namespace ya
{

struct CubeMap2PBRPrefilteredEnv
{

    using PushConstant = slang_types::Misc::CubeMap2PBRPrefilterEnv::PushConstants;

    IRender* _render = nullptr;

    PipelineLayoutDesc _pipelineLayoutDesc = PipelineLayoutDesc{
        .label         = "CubeMap2PBRPrefilterEnv_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(CubeMap2PBRPrefilteredEnv::PushConstant),
                .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment,
            },
        },
        .descriptorSetLayouts = {
            DescriptorSetLayoutDesc{
                .label    = "CubeMap2PBRPrefilterEnv_DSL",
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

    stdptr<IDescriptorSetLayout> _descriptorSetLayout = nullptr;
    stdptr<Sampler>              _inputSampler        = nullptr;
    stdptr<IPipelineLayout>      _pipelineLayout      = nullptr;
    stdptr<IGraphicsPipeline>    _pipeline            = nullptr;
    EFormat::T                   _pipelineColorFormat = EFormat::Undefined;
    std::vector<stdptr<IImageView>> _transientFaceViews;


  public:
    struct ExecuteResult
    {
        bool                               bSuccess                 = false;
        stdptr<IImageView>                 transientOutputArrayView = nullptr;
        std::vector<std::shared_ptr<void>> keepAliveResources;
    };
    struct ExecuteContext
    {
        ICommandBuffer* cmdBuf     = nullptr;
        RenderImage*    inputImage = nullptr;
        Texture*        inputTexture = nullptr;
        RenderImage*    output     = nullptr;
        ClearValue      clearColor = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
    };

    void          init(IRender* render);
    void          shutdown();
    ExecuteResult execute(const ExecuteContext& ctx);

  private:
    bool                ensurePipeline(EFormat::T colorFormat);
    static PushConstant buildPushConstant(uint32_t faceIndex, float roughness);
};

} // namespace ya
