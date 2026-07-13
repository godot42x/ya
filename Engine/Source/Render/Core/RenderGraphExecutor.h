#pragma once

#include "Render/Core/RenderGraph.h"
#include "Render/Core/RenderGraphResourceRegistry.h"
#include "Render/Core/ResourceStateTracker.h"

#include <unordered_map>

namespace ya
{

class RenderGraphExecutor
{
  private:
    std::unordered_map<IBuffer*, BufferResourceState> _bufferStates;
    RenderGraphResourceRegistry _registry;
    ResourceStateTracker        _resourceStateTracker;

  public:
    explicit RenderGraphExecutor(IRenderResourceFactory& factory)
        : _registry(factory)
    {}

    [[nodiscard]] bool execute(
        const RenderGraph& graph,
        ICommandBuffer& cmdBuf,
        RGCompiledGraph* outCompiled = nullptr);

    void clear();

    [[nodiscard]] const RenderGraphResourceRegistry& getRegistry() const { return _registry; }
};

} // namespace ya
