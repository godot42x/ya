
#pragma once

#include "Foundation/RHI/Core/CommandBuffer.h"
#include "Foundation/RHI/Core/DescriptorSet.h"
#include "Foundation/RHI/Core/Image.h"
#include "Foundation/RHI/Core/RenderImage.h"
#include "Foundation/RHI/Core/Pipeline.h"
#include "Foundation/RHI/Core/Sampler.h"
#include "Foundation/RHI/Core/Texture.h"

#include "Misc.EquidistantCylindrical2CubeMap.slang.h"

namespace ya
{

struct EquidistantCylindrical2CubeMap
{

    using PushConstant = slang_types::Misc::EquidistantCylindrical2CubeMap::PushConstants;

    IRender* _render = nullptr;

    PipelineLayoutDesc _pipelineLayoutDesc;

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
        Texture*      input         = nullptr;
        RenderImage*  output        = nullptr;
        bool          bFlipVertical = false;
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
