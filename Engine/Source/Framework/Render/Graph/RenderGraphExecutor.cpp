#include "RenderGraphExecutor.h"

#include <algorithm>
#include <unordered_set>

namespace ya
{

namespace
{

bool bufferRangesOverlap(const BufferResourceState& lhs, const BufferResourceState& rhs)
{
    return lhs.offset < rhs.offset + rhs.size && rhs.offset < lhs.offset + lhs.size;
}

BufferResourceState normalizeBufferState(const BufferResourceState& state, const IBuffer& buffer)
{
    auto normalized = state;
    if (normalized.size == 0) {
        normalized.size = buffer.getSize();
    }
    return normalized;
}

bool isTransientAliasBoundary(const RGCompiledGraph& compiled,
                              RGPassHandle pass,
                              RGBufferHandle buffer)
{
    return std::any_of(compiled.transientBufferAliasBoundaries.begin(),
                       compiled.transientBufferAliasBoundaries.end(),
                       [pass, buffer](const RGTransientBufferAliasBoundaryPlan& boundary) {
                           return boundary.nextPass == pass && boundary.nextBuffer == buffer;
                       });
}

} // namespace

const BufferResourceState* RenderGraphExecutor::findBufferState(
    const std::vector<BufferResourceState>& states,
    const BufferResourceState&              requested)
{
    for (const auto& state : states) {
        if (state.offset == requested.offset && state.size == requested.size) {
            return &state;
        }
    }

    for (const auto& state : states) {
        if (bufferRangesOverlap(state, requested)) {
            return &state;
        }
    }
    return nullptr;
}

void RenderGraphExecutor::setBufferState(
    std::vector<BufferResourceState>& states,
    const BufferResourceState&         state)
{
    for (auto& existing : states) {
        if (existing.offset == state.offset && existing.size == state.size) {
            existing = state;
            return;
        }
    }
    states.push_back(state);
}

bool RenderGraphExecutor::prepare(
    const RenderGraph& graph,
    RGCompiledGraph&   outCompiled,
    RenderGraphExecutionResult* outResult)
{
    _bufferStates.clear();

    outCompiled = graph.compile();
    if (!outCompiled.isValid()) {
        if (outResult) {
            outResult->clear();
        }
        YA_CORE_ERROR("RenderGraph compile failed:\n{}", graph.debugDump(outCompiled));
        return false;
    }

    _registry.sync(graph, &outCompiled);
    if (outResult) {
        captureExecutionResult(outCompiled, *outResult);
    }
    return true;
}

bool RenderGraphExecutor::executeCompiled(
    const RenderGraph&    graph,
    const RGCompiledGraph& compiled,
    ICommandBuffer&       cmdBuf)
{
    for (const auto& passPlan : compiled.passPlans) {
        const auto* pass = graph.getPass(passPlan.pass);
        YA_CORE_ASSERT(pass != nullptr, "RenderGraphExecutor encountered invalid pass handle {}", passPlan.pass.index);
        std::unordered_set<RGBufferHandle> aliasBoundaryBarriersEmitted;

        for (const auto& statePlan : passPlan.textureStates) {

            const auto* texture = _registry.resolveTexture(statePlan.texture);
            YA_CORE_ASSERT(texture != nullptr, "RenderGraphExecutor failed to resolve texture {}", statePlan.texture.index);
            YA_CORE_ASSERT(texture->getImage() != nullptr, "RenderGraphExecutor texture {} has no backing image", statePlan.texture.index);

            cmdBuf.transitionImageLayoutAuto(
                texture->getImage(),
                statePlan.layout,
                &statePlan.subresourceRange);
        }

        for (const auto& statePlan : passPlan.bufferStates) {

            auto* buffer = _registry.resolveBuffer(statePlan.buffer);
            YA_CORE_ASSERT(buffer != nullptr, "RenderGraphExecutor failed to resolve buffer {}", statePlan.buffer.index);
            if (const auto* resource = graph.getBuffer(statePlan.buffer);
                resource && resource->imported.has_value()) {
                cmdBuf.retainResources(resource->imported->retainedResources);
            }

            const auto newState = normalizeBufferState(statePlan.requiredState, *buffer);
            BufferResourceState oldState{};
            const auto statesIt = _bufferStates.find(buffer);
            const auto* priorState = statesIt != _bufferStates.end()
                ? findBufferState(statesIt->second, newState)
                : nullptr;
            if (priorState) {
                oldState = *priorState;
            }
            else if (const auto* resource = graph.getBuffer(statePlan.buffer);
                     resource && resource->imported.has_value()) {
                oldState = normalizeBufferState(resource->imported->initialState, *buffer);
            }

            const bool bAliasBoundary =
                !aliasBoundaryBarriersEmitted.contains(statePlan.buffer) &&
                isTransientAliasBoundary(compiled, passPlan.pass, statePlan.buffer);
            const bool bNeedsBarrier =
                bAliasBoundary ||
                oldState.stages != newState.stages ||
                oldState.access != newState.access ||
                oldState.offset != newState.offset ||
                oldState.size != newState.size;

            if (bNeedsBarrier) {
                const auto barrierOffset = bAliasBoundary ? 0 : newState.offset;
                const auto barrierSize = bAliasBoundary ? buffer->getSize() : newState.size;
                cmdBuf.bufferMemoryBarrier(
                    buffer,
                    oldState.stages,
                    newState.stages,
                    oldState.access,
                    newState.access,
                    barrierOffset,
                    barrierSize);
                if (bAliasBoundary) {
                    aliasBoundaryBarriersEmitted.insert(statePlan.buffer);
                    _bufferStates[buffer].clear();
                }
            }

            setBufferState(_bufferStates[buffer], newState);
        }

        if (!pass->execute) {
            continue;
        }

        RGRenderContext ctx(graph, *pass, _registry, &passPlan, cmdBuf);
        pass->execute(ctx);
    }

    finalizeImportedBufferStates(compiled, cmdBuf);
    finalizeImportedTextureStates(compiled, cmdBuf);
    return true;
}

void RenderGraphExecutor::finalizeImportedBufferStates(const RGCompiledGraph& compiled, ICommandBuffer& cmdBuf)
{
    for (const auto& finalize : compiled.importedBufferFinalizes) {
        auto* buffer = _registry.resolveBuffer(finalize.buffer);
        YA_CORE_ASSERT(buffer != nullptr, "RenderGraphExecutor failed to resolve imported buffer {}", finalize.buffer.index);

        const auto newState = normalizeBufferState(finalize.finalState, *buffer);
        BufferResourceState oldState{};
        const auto statesIt = _bufferStates.find(buffer);
        const auto* priorState = statesIt != _bufferStates.end()
            ? findBufferState(statesIt->second, newState)
            : nullptr;
        if (priorState) {
            oldState = *priorState;
        }
        else {
            oldState = normalizeBufferState(finalize.initialState, *buffer);
        }

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

        setBufferState(_bufferStates[buffer], newState);
    }
}

void RenderGraphExecutor::finalizeImportedTextureStates(const RGCompiledGraph& compiled, ICommandBuffer& cmdBuf)
{
    for (const auto& finalize : compiled.importedTextureFinalizes) {
        const auto* texture = _registry.resolveTexture(finalize.texture);
        YA_CORE_ASSERT(texture != nullptr, "RenderGraphExecutor failed to resolve imported texture {}", finalize.texture.index);
        YA_CORE_ASSERT(texture->getImage() != nullptr, "RenderGraphExecutor imported texture {} has no backing image", finalize.texture.index);

        const ImageSubresourceRange* subresourceRange =
            finalize.subresourceRange ? &*finalize.subresourceRange : nullptr;
        cmdBuf.transitionImageLayoutAuto(texture->getImage(), finalize.finalLayout, subresourceRange);
    }
}

bool RenderGraphExecutor::execute(
    const RenderGraph& graph,
    ICommandBuffer& cmdBuf,
    RGCompiledGraph* outCompiled,
    RenderGraphExecutionResult* outResult)
{
    RGCompiledGraph compiled{};
    if (!prepare(graph, compiled, outResult)) {
        if (outCompiled) {
            *outCompiled = compiled;
        }
        return false;
    }
    if (outCompiled) {
        *outCompiled = compiled;
    }
    if (!executeCompiled(graph, compiled, cmdBuf)) {
        return false;
    }
    return true;
}

void RenderGraphExecutor::captureExecutionResult(const RGCompiledGraph& compiled, RenderGraphExecutionResult& outResult) const
{
    outResult.clear();
    for (const auto& exported : compiled.exportedTextures) {
        outResult.bindExportedTexture(exported.name, _registry.resolveTextureShared(exported.texture));
    }
}

void RenderGraphExecutor::clear()
{
    _bufferStates.clear();
    _registry.clear();
}

} // namespace ya
