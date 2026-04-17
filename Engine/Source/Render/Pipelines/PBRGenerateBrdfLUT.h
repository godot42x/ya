
#pragma once

#include "Render/Core/Pipeline.h"
#include "Render/Core/Texture.h"
#include "Render/Render.h"

namespace ya
{

struct PBRGenerateBrdfLUT
{

    IRender* _render = nullptr;

    PipelineLayoutDesc _pipelineLayoutDesc = PipelineLayoutDesc{
        .label                = "PBRGenerateBrdfLUT_PipelineLayout",
        .pushConstants        = {},
        .descriptorSetLayouts = {},
    };

    stdptr<IPipelineLayout>   _pipelineLayout      = nullptr;
    stdptr<IGraphicsPipeline> _pipeline            = nullptr;
    EFormat::T                _pipelineColorFormat = EFormat::Undefined;

    struct ExecuteResult
    {
        bool bSuccess = false;
    };

    struct ExecuteContext
    {
        ICommandBuffer* cmdBuf     = nullptr;
        Texture*        output     = nullptr;
        ClearValue      clearColor = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
    };

  public:
    void          init(IRender* render);
    void          shutdown();
    ExecuteResult execute(const ExecuteContext& ctx);

  private:
    bool ensurePipeline(EFormat::T colorFormat);
};

} // namespace ya