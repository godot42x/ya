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

    _registry.sync(graph);
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

        for (const auto& statePlan : passPlan.textureStates) {

            const auto* texture = _registry.resolveTexture(statePlan.texture);
            YA_CORE_ASSERT(texture != nullptr, "RenderGraphExecutor failed to resolve texture {}", statePlan.texture.index);
            YA_CORE_ASSERT(texture->getImage() != nullptr, "RenderGraphExecutor texture {} has no backing image", statePlan.texture.index);

            cmdBuf.transitionImageLayoutAuto(
                texture->getImage(),
                statePlan.requiredState.layout,
                &statePlan.requiredState.subresourceRange);
        }

        for (const auto& statePlan : passPlan.bufferStates) {

            auto* buffer = _registry.resolveBuffer(statePlan.buffer);
            YA_CORE_ASSERT(buffer != nullptr, "RenderGraphExecutor failed to resolve buffer {}", statePlan.buffer.index);
            if (const auto* resource = graph.getBuffer(statePlan.buffer);
                resource && resource->imported.has_value()) {
                cmdBuf.retainResources(resource->imported->retainedResources);
            }

            const auto newState = normalizeBufferState(statePlan.requiredState, *buffer);
            const auto oldIt = _bufferStates.find(buffer);
            BufferResourceState oldState{};
            if (oldIt != _bufferStates.end()) {
                oldState = oldIt->second;
            }
            else if (const auto* resource = graph.getBuffer(statePlan.buffer);
                     resource && resource->imported.has_value()) {
                oldState = normalizeBufferState(resource->imported->initialState, *buffer);
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

            _bufferStates[buffer] = newState;
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
        const auto oldIt    = _bufferStates.find(buffer);
        BufferResourceState oldState{};
        if (oldIt != _bufferStates.end()) {
            oldState = oldIt->second;
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

        _bufferStates[buffer] = newState;
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
