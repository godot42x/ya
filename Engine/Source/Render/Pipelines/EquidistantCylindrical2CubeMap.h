
#pragma once

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Image.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Sampler.h"
#include "Render/Core/Texture.h"

#include "Misc.EquidistantCylindrical2CubeMap.slang.h"

namespace ya
{

struct EquidistantCylindrical2CubeMap
{

    using PushConstant = slang_types::Misc::EquidistantCylindrical2CubeMap::PushConstants;

    IRender* _render = nullptr;

    PipelineLayoutDesc _pipelineLayoutDesc;

    stdptr<IDescriptorSetLayout> _descriptorSetLayout = nullptr;
    stdptr<IDescriptorPool>      _descriptorPool      = nullptr;
    stdptr<Sampler>              _inputSampler        = nullptr;
    stdptr<IPipelineLayout>      _pipelineLayout      = nullptr;
    stdptr<IGraphicsPipeline>    _pipeline            = nullptr;
    DescriptorSetHandle          _descriptorSet       = nullptr;
    EFormat::T                   _pipelineColorFormat = EFormat::Undefined;


  public:
    struct ExecuteResult
    {
        bool               bSuccess                 = false;
        stdptr<IImageView> transientOutputArrayView = nullptr;
    };
    struct ExecuteContext
    {
        ICommandBuffer* cmdBuf     = nullptr;
        Texture*        input      = nullptr;
        Texture*        output     = nullptr;
      bool            bFlipVertical = false;
        ClearValue      clearColor = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
    };

    void          init(IRender* render);
    void          shutdown();
    ExecuteResult execute(const ExecuteContext& ctx);

  private:
    bool                ensurePipeline(EFormat::T colorFormat);
    static PushConstant buildPushConstant(uint32_t faceIndex, bool bFlipVertical);
};

} // namespace ya