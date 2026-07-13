#include "RenderGraphExecutor.h"

namespace ya
{

namespace
{

BufferResourceState normalizeBufferState(const BufferResourceState& state, const IBuffer& buffer)
{
    auto normalized = state;
    if (normalized.size == 0) {
        normalized.size = buffer.getSize();
    }
    return normalized;
}

} // namespace

bool RenderGraphExecutor::prepare(
    const RenderGraph& graph,
    RGCompiledGraph&   outCompiled)
{
    _bufferStates.clear();
    _resourceStateTracker.reset();

    outCompiled = graph.compile();
    if (!outCompiled.isValid()) {
        YA_CORE_ERROR("RenderGraph compile failed:\n{}", graph.debugDump(outCompiled));
        return false;
    }

    _registry.sync(graph);
    return true;
}

bool RenderGraphExecutor::executeCompiled(
    const RenderGraph&    graph,
    const RGCompiledGraph& compiled,
    ICommandBuffer&       cmdBuf)
{
    for (const auto& passHandle : compiled.order) {
        const auto* pass = graph.getPass(passHandle);
        YA_CORE_ASSERT(pass != nullptr, "RenderGraphExecutor encountered invalid pass handle {}", passHandle.index);

        for (const auto& statePlan : compiled.textureStates) {
            if (statePlan.pass != passHandle) {
                continue;
            }

            const auto* texture = _registry.resolveTexture(statePlan.texture);
            YA_CORE_ASSERT(texture != nullptr, "RenderGraphExecutor failed to resolve texture {}", statePlan.texture.index);
            YA_CORE_ASSERT(texture->getImage() != nullptr, "RenderGraphExecutor texture {} has no backing image", statePlan.texture.index);

            const auto transitions = _resourceStateTracker.transition(
                *texture->getImage(),
                statePlan.requiredState,
                &statePlan.requiredState.subresourceRange);
            for (const auto& transition : transitions) {
                cmdBuf.transitionImageLayout(
                    transition.image,
                    transition.oldState.layout,
                    transition.newState.layout,
                    &transition.range);
            }
        }

        for (const auto& statePlan : compiled.bufferStates) {
            if (statePlan.pass != passHandle) {
                continue;
            }

            auto* buffer = _registry.resolveBuffer(statePlan.buffer);
            YA_CORE_ASSERT(buffer != nullptr, "RenderGraphExecutor failed to resolve buffer {}", statePlan.buffer.index);

            const auto newState = normalizeBufferState(statePlan.requiredState, *buffer);
            const auto oldIt    = _bufferStates.find(buffer);
            const auto oldState = oldIt != _bufferStates.end() ? oldIt->second : BufferResourceState{};

            const bool bNeedsBarrier =
                oldState.stages != newState.stages ||
                oldState.access != newState.access ||
                oldState.offset != newState.offset ||
                oldState.size != newState.size;

            if (bNeedsBarrier) {
                cmdBuf.bufferMemoryBarrier(
                    buffer,
                    oldState.stages,
                    newState.stages,
                    oldState.access,
                    newState.access,
                    newState.offset,
                    newState.size);
            }

            _bufferStates[buffer] = newState;
        }

        if (!pass->execute) {
            continue;
        }

        RGRenderContext ctx(graph, *pass, _registry, cmdBuf);
        pass->execute(ctx);
    }

    return true;
}

bool RenderGraphExecutor::execute(
    const RenderGraph& graph,
    ICommandBuffer& cmdBuf,
    RGCompiledGraph* outCompiled)
{
    RGCompiledGraph compiled{};
    if (!prepare(graph, compiled)) {
        if (outCompiled) {
            *outCompiled = compiled;
        }
        return false;
    }
    if (outCompiled) {
        *outCompiled = compiled;
    }
    return executeCompiled(graph, compiled, cmdBuf);
}

void RenderGraphExecutor::clear()
{
    _bufferStates.clear();
    _registry.clear();
}

} // namespace ya
