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

    void finalizeImportedBufferStates(const RenderGraph& graph, ICommandBuffer& cmdBuf);
    void finalizeImportedTextureStates(const RenderGraph& graph, ICommandBuffer& cmdBuf);

  public:
    explicit RenderGraphExecutor(IRenderResourceFactory& factory)
        : _registry(factory)
    {}

    [[nodiscard]] bool prepare(
        const RenderGraph& graph,
        RGCompiledGraph&   outCompiled);

    [[nodiscard]] bool executeCompiled(
        const RenderGraph&  graph,
        const RGCompiledGraph& compiled,
        ICommandBuffer&     cmdBuf);

    [[nodiscard]] bool execute(
        const RenderGraph& graph,
        ICommandBuffer& cmdBuf,
        RGCompiledGraph* outCompiled = nullptr);

    void clear();

    [[nodiscard]] const RenderGraphResourceRegistry& getRegistry() const { return _registry; }
};

} // namespace ya
