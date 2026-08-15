#pragma once

#include "Graph/RenderGraph.h"
#include "Core/Api.h"
#include "Graph/RenderGraphResourceRegistry.h"
#include "RHI/Core/ResourceStateTracker.h"

#include <unordered_map>
#include <vector>

namespace ya
{

class YA_RENDER_GRAPH_API RenderGraphExecutor
{
  private:
    std::unordered_map<IBuffer*, std::vector<BufferResourceState>> _bufferStates;
    RenderGraphResourceRegistry _registry;
    ResourceStateTracker        _resourceStateTracker;

    [[nodiscard]] static const BufferResourceState* findBufferState(
        const std::vector<BufferResourceState>& states,
        const BufferResourceState&              requested);
    static void setBufferState(
        std::vector<BufferResourceState>& states,
        const BufferResourceState&         state);

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
