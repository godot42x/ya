
#pragma once

#include "Graph/RenderGraphExecutor.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Render.h"

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
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;

    struct ExecuteResult
    {
        bool bSuccess = false;
    };

    struct ExecuteContext
    {
        ICommandBuffer* cmdBuf     = nullptr;
        RenderTexture*  output     = nullptr;
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
