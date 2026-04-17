#pragma once

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Sampler.h"
#include "Render/Core/Texture.h"

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
    stdptr<IDescriptorPool>      _descriptorPool      = nullptr;
    stdptr<Sampler>              _inputSampler        = nullptr;
    stdptr<IPipelineLayout>      _pipelineLayout      = nullptr;
    stdptr<IGraphicsPipeline>    _pipeline            = nullptr;
    DescriptorSetHandle          _descriptorSet       = nullptr;
    EFormat::T                   _pipelineColorFormat = EFormat::Undefined;

    struct ExecuteResult
    {
        bool bSuccess = false;
    };

    struct ExecuteContext
    {
        ICommandBuffer* cmdBuf     = nullptr;
        Texture*        input      = nullptr; // the cubemap
        Texture*        output     = nullptr; // the irradiance map to render to (must be a cubemap with 6 array layers)
        ClearValue      clearColor = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
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