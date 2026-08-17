#pragma once

#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/Core/ImageResource.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Sampler.h"
#include "RHI/Core/Texture.h"

#include "Misc.CubeMap2PBRIrradianceMap.slang.h"

namespace ya
{


struct CubeMap2PBRIrradianceMap
{
    using PushConstant = slang_types::Misc::CubeMap2PBRIrradianceMap::PushConstants;

    IRender* _render = nullptr;

    PipelineLayoutDesc _pipelineLayoutDesc = PipelineLayoutDesc{
        .label         = "CubeMap2PBRIrradianceMap_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(CubeMap2PBRIrradianceMap::PushConstant),
                .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment,
            },
        },
        .descriptorSetLayouts = {
            DescriptorSetLayoutDesc{
                .label    = "CubeMap2PBRIrradianceMap_DSL",
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
    DescriptorPoolCreateInfo _dspCI{
        .label     = "CubeMap2PBRIrradianceMap_DSP",
        .maxSets   = 1,
        .poolSizes = {
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::CombinedImageSampler,
                .descriptorCount = 1,
            },
        },
    };

    stdptr<IDescriptorSetLayout> _descriptorSetLayout = nullptr;
    stdptr<Sampler>              _inputSampler        = nullptr;
    stdptr<IPipelineLayout>      _pipelineLayout      = nullptr;
    stdptr<IGraphicsPipeline>    _pipeline            = nullptr;
    EFormat::T                   _pipelineColorFormat = EFormat::Undefined;
    std::vector<stdptr<IImageView>> _transientFaceViews;

    struct ExecuteResult
    {
        bool                               bSuccess = false;
        std::vector<std::shared_ptr<void>> keepAliveResources;
    };

    struct ExecuteContext
    {
        ICommandBuffer* cmdBuf     = nullptr;
        ImageResource* input  = nullptr; // owner-first input cubemap (Texture or RenderTexture resource)
        ImageResource* output = nullptr; // the irradiance map to render to (must be a cubemap with 6 array layers)
        ClearValue     clearColor  = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
    };

  public:
    void          init(IRender* render);
    void          shutdown();
    ExecuteResult execute(const ExecuteContext& ctx);

  private:
    bool                ensurePipeline(EFormat::T colorFormat);
    static PushConstant buildPushConstant(uint32_t faceIndex);
};



}; // namespace ya
