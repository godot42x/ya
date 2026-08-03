#pragma once

#include "Render/Core/RenderGraph.h"
#include "Core/Api.h"
#include "Render/Core/RenderGraphResourceRegistry.h"
#include "Render/Core/ResourceStateTracker.h"

#include <unordered_map>

namespace ya
{

class ENGINE_API RenderGraphExecutor
{
  private:
    std::unordered_map<IBuffer*, BufferResourceState> _bufferStates;
    RenderGraphResourceRegistry _registry;
    ResourceStateTracker        _resourceStateTracker;

    void finalizeImportedBufferStates(const RGCompiledGraph& compiled, ICommandBuffer& cmdBuf);
    void finalizeImportedTextureStates(const RGCompiledGraph& compiled, ICommandBuffer& cmdBuf);
    void captureExecutionResult(const RGCompiledGraph& compiled, RenderGraphExecutionResult& outResult) const;

  public:
    explicit RenderGraphExecutor(IRenderResourceFactory& factory)
        : _registry(factory)
    {}

    [[nodiscard]] bool prepare(
        const RenderGraph& graph,
        RGCompiledGraph&   outCompiled,
        RenderGraphExecutionResult* outResult = nullptr);

    [[nodiscard]] bool executeCompiled(
        const RenderGraph&  graph,
        const RGCompiledGraph& compiled,
        ICommandBuffer&     cmdBuf);

    [[nodiscard]] bool execute(
        const RenderGraph& graph,
        ICommandBuffer& cmdBuf,
        RGCompiledGraph* outCompiled = nullptr,
        RenderGraphExecutionResult* outResult = nullptr);

    void clear();

    [[nodiscard]] const RenderGraphResourceRegistry& getRegistry() const { return _registry; }
};

} // namespace ya
