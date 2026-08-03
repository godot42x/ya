#include "PointShadowCullPass.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Resource/DeferredDeletionQueue.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"

#include <format>
#include <limits>

namespace ya
{

void PointShadowCullPass::init(IRender* render)
{
    _render = render;
    _graphExecutor = std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory());

    _cullDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "PointShadowCull_DSL",
            .set      = 0,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Compute},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Compute},
                {.binding = 2, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Compute},
                {.binding = 3, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Compute},
            },
        });

    _pipelineLayout = IPipelineLayout::create(
        _render, "PointShadowCull_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(PushConstants), .stageFlags = EShaderStage::Compute}},
        {_cullDSL});

    _pipeline = IComputePipeline::create(_render);
    YA_CORE_ASSERT(_pipeline && _pipeline->recreate(ComputePipelineCreateInfo{
                       .pipelineLayout = _pipelineLayout.get(),
                       .shaderDesc     = ShaderDesc{.shaderName = "Shadow/PointShadowCull.comp.slang"},
                   }),
                   "Failed to create point shadow cull pipeline");

    _dsp = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                .label     = "PointShadowCull_DSP",
                                                .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
                                                .poolSizes = {{.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT * 4}},
                                            });

    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _perFlight[i].cullDS = _dsp->allocateDescriptorSets(_cullDSL);
    }
}

void PointShadowCullPass::destroy()
{
    for (auto& flight : _perFlight) {
        flight.faceFrustumBuffer.reset();
        flight.drawCommandBuffer.reset();
        flight.visibleInstancesBuf.reset();
        flight.instanceBuffer.reset();
        flight.cullDS = nullptr;
        flight.allocatedBucketCount = 0;
    }
    _dsp.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
    _cullDSL.reset();
    _graphExecutor.reset();
    _render               = nullptr;
}

bool PointShadowCullPass::ensureCapacity(uint32_t flightIndex, uint32_t bucketCount)
{
    YA_PROFILE_FUNCTION();
    if (flightIndex >= _perFlight.size()) return false;
    auto& flight = _perFlight[flightIndex];
    if (bucketCount == 0) return true;
    if (bucketCount <= flight.allocatedBucketCount && flight.drawCommandBuffer && flight.visibleInstancesBuf && flight.faceFrustumBuffer) {
        return true;
    }

    const uint64_t cmdBytes = static_cast<uint64_t>(bucketCount) * sizeof(PointShadowIndirectCommand);
    const uint64_t visibleBytes64 = static_cast<uint64_t>(bucketCount) * ShadowConstants::MAX_DRAWS_PER_FACE * sizeof(uint32_t);
    const uint64_t frustumBytes = static_cast<uint64_t>(bucketCount) * sizeof(PointShadowFaceFrustum);
    if (cmdBytes > std::numeric_limits<uint32_t>::max() ||
        visibleBytes64 > std::numeric_limits<uint32_t>::max() ||
        frustumBytes > std::numeric_limits<uint32_t>::max()) {
        YA_CORE_ERROR("Point shadow cull bucket count {} exceeds buffer size limits", bucketCount);
        return false;
    }

    auto nextFrustum = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
            .label       = std::format("PointShadowCull_Frustum_{}", flightIndex),
            .usage       = EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
            .size        = static_cast<uint32_t>(frustumBytes),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
    // Host-visible: CPU writes the static fields once per frame; if the
    // compute path is active it atomically updates instanceCount on top.
    auto nextDrawCommands = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
            .label       = std::format("PointShadowCull_DrawCmd_{}", flightIndex),
            .usage       = EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer | EBufferUsage::TransferSrc,
            .size        = static_cast<uint32_t>(cmdBytes),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
    auto nextVisibleInstances = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
            .label       = std::format("PointShadowCull_VisInst_{}", flightIndex),
            .usage       = EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
            .size        = static_cast<uint32_t>(visibleBytes64),
            .memoryUsage = EMemoryUsage::CpuToGpu, // host-visible to allow CPU NoCull writes
        });
    if (!nextFrustum || !nextDrawCommands || !nextVisibleInstances) {
        YA_CORE_ERROR("PointShadowCullPass failed to allocate buffers for flight {}", flightIndex);
        return false;
    }

    auto oldFrustum = std::move(flight.faceFrustumBuffer);
    auto oldDrawCommands = std::move(flight.drawCommandBuffer);
    auto oldVisibleInstances = std::move(flight.visibleInstancesBuf);
    flight.faceFrustumBuffer = std::move(nextFrustum);
    flight.drawCommandBuffer = std::move(nextDrawCommands);
    flight.visibleInstancesBuf = std::move(nextVisibleInstances);
    flight.allocatedBucketCount = bucketCount;

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 1, flight.faceFrustumBuffer.get()),
        IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 2, flight.drawCommandBuffer.get()),
        IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 3, flight.visibleInstancesBuf.get()),
    });

    if (DeferredDeletionQueue::get().isInitialized()) {
        DeferredDeletionQueue::get().retireResource(std::move(oldFrustum));
        DeferredDeletionQueue::get().retireResource(std::move(oldDrawCommands));
        DeferredDeletionQueue::get().retireResource(std::move(oldVisibleInstances));
    }
    return true;
}

void PointShadowCullPass::bindInstanceBuffer(uint32_t flightIndex, const stdptr<IBuffer>& instanceBuffer)
{
    if (!instanceBuffer || !_render) return;
    _perFlight[flightIndex].instanceBuffer = instanceBuffer;
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneStorageBuffer(_perFlight[flightIndex].cullDS, 0, instanceBuffer.get()),
    });
}

void PointShadowCullPass::writeDrawCommandTemplate(uint32_t                          flightIndex,
                                                    const PointShadowIndirectCommand* cmds,
                                                    uint32_t                          bucketCount)
{
    YA_PROFILE_FUNCTION();
    if (bucketCount == 0) return;
    auto& flight = _perFlight[flightIndex];
    if (!flight.drawCommandBuffer) return;
    flight.drawCommandBuffer->writeData(cmds, bucketCount * sizeof(PointShadowIndirectCommand), 0);
    flight.drawCommandBuffer->flush();
}

void PointShadowCullPass::writeVisibleInstances(uint32_t        flightIndex,
                                                const uint32_t* data,
                                                uint32_t        count)
{
    YA_PROFILE_FUNCTION();
    if (count == 0) return;
    auto& flight = _perFlight[flightIndex];
    if (!flight.visibleInstancesBuf) return;
    flight.visibleInstancesBuf->writeData(data, count * sizeof(uint32_t), 0);
    flight.visibleInstancesBuf->flush();
}

void PointShadowCullPass::prepareCompute(uint32_t                      flightIndex,
                                          const PointShadowFaceFrustum* faceFrustums,
                                          uint32_t                      activeFaceCount,
                                          uint32_t                      instanceCount,
                                          uint32_t                      batchCount)
{
    YA_PROFILE_FUNCTION();
    auto& flight            = _perFlight[flightIndex];
    flight.activeFaceCount  = activeFaceCount;
    flight.activeBatchCount = batchCount;
    flight.instanceCount    = instanceCount;
    if (activeFaceCount == 0 || instanceCount == 0 || batchCount == 0) return;

    flight.faceFrustumBuffer->writeData(faceFrustums, activeFaceCount * sizeof(PointShadowFaceFrustum), 0);
    flight.faceFrustumBuffer->flush();
}

void PointShadowCullPass::prepareNoCull(uint32_t flightIndex, uint32_t activeFaceCount, uint32_t batchCount)
{
    if (flightIndex >= _perFlight.size()) return;
    auto& flight            = _perFlight[flightIndex];
    flight.activeFaceCount  = activeFaceCount;
    flight.activeBatchCount = batchCount;
    flight.instanceCount    = 0;
}

void PointShadowCullPass::dispatch(ICommandBuffer* cmdBuf, uint32_t flightIndex)
{
    YA_PROFILE_FUNCTION();
    if (!cmdBuf || !_graphExecutor) return;

    RenderGraph graph;
    if (!appendGraphPass(graph, flightIndex, true).has_value()) return;

    YA_CORE_ASSERT(_graphExecutor->execute(graph, *cmdBuf),
                   "Failed to execute point shadow cull graph");
}

std::optional<PointShadowCullPass::GraphResources> PointShadowCullPass::appendGraphPass(
    RenderGraph& graph,
    uint32_t flightIndex,
    bool bDispatchCull,
    std::optional<RGPassHandle> dependency)
{
    if (flightIndex >= _perFlight.size()) return std::nullopt;
    const auto& flight = _perFlight[flightIndex];
    if (!flight.drawCommandBuffer || !flight.visibleInstancesBuf) return std::nullopt;
    if (bDispatchCull &&
        (!flight.instanceBuffer || !flight.faceFrustumBuffer ||
         flight.activeFaceCount == 0 || flight.instanceCount == 0 || flight.activeBatchCount == 0)) {
        return std::nullopt;
    }

    auto importBuffer = [](RenderGraph& graph,
                           const std::shared_ptr<IBuffer>& buffer,
                           std::string label,
                           EBufferUsage usage,
                           BufferResourceState initialState,
                           std::optional<BufferResourceState> finalState = std::nullopt) {
        YA_CORE_ASSERT(buffer != nullptr, "Point shadow cull graph requires imported buffer '{}'", label);
        return graph.importBuffer(RGImportedBufferDesc{
            .desc = RGBufferDesc{
                .label = std::move(label),
                .usage = usage,
                .size  = buffer->getSize(),
            },
            .buffer            = buffer.get(),
            .initialState      = initialState,
            .finalState        = finalState,
            .retainedResources = {buffer},
        });
    };

    const BufferResourceState hostWriteState{
        .stages = EPipelineStage::Host,
        .access = EResourceAccess::HostWrite,
    };
    const uint64_t bucketCount = static_cast<uint64_t>(flight.activeFaceCount) * flight.activeBatchCount;
    const uint64_t commandBytes = bucketCount * sizeof(PointShadowIndirectCommand);
    const uint64_t visibleBytes = bucketCount * ShadowConstants::MAX_DRAWS_PER_FACE * sizeof(uint32_t);
    const uint64_t frustumBytes = static_cast<uint64_t>(flight.activeFaceCount) * sizeof(PointShadowFaceFrustum);
    if (bucketCount == 0 || commandBytes > std::numeric_limits<uint32_t>::max() ||
        visibleBytes > std::numeric_limits<uint32_t>::max() ||
        (bDispatchCull && frustumBytes > std::numeric_limits<uint32_t>::max())) {
        return std::nullopt;
    }

    const auto drawCommandStaging = importBuffer(
        graph,
        flight.drawCommandBuffer,
        "PointShadowCull.DrawCommands",
        EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer | EBufferUsage::TransferSrc,
        hostWriteState);
    const auto visibleInstancesStaging = importBuffer(
        graph,
        flight.visibleInstancesBuf,
        "PointShadowCull.VisibleInstances",
        EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
        hostWriteState);
    const auto drawCommands = graph.createBuffer(RGBufferDesc{
        .label = "PointShadowCull.DrawCommands.Transient",
        .usage = EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer | EBufferUsage::TransferDst,
        .size  = static_cast<uint32_t>(commandBytes),
    });
    const auto visibleInstances = graph.createBuffer(RGBufferDesc{
        .label = "PointShadowCull.VisibleInstances.Transient",
        .usage = EBufferUsage::StorageBuffer | EBufferUsage::TransferDst,
        .size  = static_cast<uint32_t>(visibleBytes),
    });

    GraphResources resources{
        .drawCommands     = drawCommands,
        .visibleInstances = visibleInstances,
    };

    std::optional<RGBufferHandle> frustumBuffer;
    std::optional<RGBufferHandle> frustumStaging;
    if (bDispatchCull) {
        frustumStaging = importBuffer(
            graph,
            flight.faceFrustumBuffer,
            "PointShadowCull.Frustums",
            EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
            hostWriteState);
        frustumBuffer = graph.createBuffer(RGBufferDesc{
            .label = "PointShadowCull.Frustums.Transient",
            .usage = EBufferUsage::StorageBuffer | EBufferUsage::TransferDst,
            .size  = static_cast<uint32_t>(frustumBytes),
        });
    }
    const auto frustumHandle = frustumBuffer.value_or(RGBufferHandle{});

    const auto uploadPass = graph.addPass(
        "Point Shadow Cull Upload",
        [drawCommandStaging, drawCommands, visibleInstancesStaging, visibleInstances,
         frustumStaging, frustumBuffer, bDispatchCull, dependency](RGPassBuilder& pass) {
            if (dependency.has_value()) pass.dependsOn(*dependency);
            pass.declareCopy();
            pass.transferSrc(drawCommandStaging);
            pass.transferDst(drawCommands);
            if (!bDispatchCull) {
                pass.transferSrc(visibleInstancesStaging);
                pass.transferDst(visibleInstances);
            }
            if (bDispatchCull) {
                pass.transferSrc(*frustumStaging);
                pass.transferDst(*frustumBuffer);
            }
        },
        [drawCommandStaging, drawCommands, visibleInstancesStaging, visibleInstances,
         frustumStaging, frustumBuffer, bDispatchCull, commandBytes, visibleBytes, frustumBytes](RGRenderContext& ctx) {
            ctx.copyBuffer(drawCommandStaging, drawCommands, commandBytes);
            if (!bDispatchCull) {
                ctx.copyBuffer(visibleInstancesStaging, visibleInstances, visibleBytes);
            }
            if (bDispatchCull) {
                ctx.copyBuffer(*frustumStaging, *frustumBuffer, frustumBytes);
            }
        });

    if (!bDispatchCull) {
        resources.cullPass = uploadPass;
        return resources;
    }

    const auto instanceBuffer = importBuffer(
        graph, flight.instanceBuffer, "PointShadowCull.Instances", EBufferUsage::StorageBuffer, hostWriteState);

    PushConstants pc{
        .instanceCount = flight.instanceCount,
        .faceCount     = flight.activeFaceCount,
        .batchCount    = flight.activeBatchCount,
        ._pad          = 0,
    };
    const uint32_t groupsX = (flight.instanceCount + ShadowConstants::CULL_WORKGROUP_SIZE - 1) / ShadowConstants::CULL_WORKGROUP_SIZE;
    const auto cullPass = graph.addPass(
        "Point Shadow Cull",
        [instanceBuffer, frustumHandle, drawCommands, visibleInstances, uploadPass](RGPassBuilder& pass) {
            pass.dependsOn(uploadPass);
            pass.declareCompute();
            pass.storageRead(instanceBuffer);
            pass.storageRead(frustumHandle);
            pass.storageReadWrite(drawCommands);
            pass.storageWrite(visibleInstances);
        },
        [this, cullDS = flight.cullDS, instanceBuffer, frustumHandle, drawCommands, visibleInstances,
         pc, groupsX, faceCount = flight.activeFaceCount](RGRenderContext& ctx) {
            YA_PROFILE_SCOPE("PointShadowPass::CullDispatch");
            YA_PERF_SCOPE(perf::sample::shadowPointCull(), perf::metric::cpuTimeMs(), perf::domain::render());
            _render->getDescriptorHelper()->updateDescriptorSets({
                IDescriptorSetHelper::writeOneStorageBuffer(cullDS, 0, ctx.resolveBuffer(instanceBuffer)),
                IDescriptorSetHelper::writeOneStorageBuffer(cullDS, 1, ctx.resolveBuffer(frustumHandle)),
                IDescriptorSetHelper::writeOneStorageBuffer(cullDS, 2, ctx.resolveBuffer(drawCommands)),
                IDescriptorSetHelper::writeOneStorageBuffer(cullDS, 3, ctx.resolveBuffer(visibleInstances)),
            });
            auto& commandBuffer = ctx.getCommandBuffer();
            commandBuffer.bindComputePipeline(_pipeline.get());
            commandBuffer.bindComputeDescriptorSets(_pipelineLayout.get(), 0, {cullDS});
            commandBuffer.pushConstants(_pipelineLayout.get(), EShaderStage::Compute, 0, sizeof(PushConstants), &pc);
            commandBuffer.dispatch(groupsX, faceCount, 1);
        });
    resources.cullPass = cullPass;
    return resources;
}

IBuffer* PointShadowCullPass::getDrawCommandBuffer(uint32_t flightIndex) const
{
    return _perFlight[flightIndex].drawCommandBuffer.get();
}

IBuffer* PointShadowCullPass::getVisibleInstancesBuffer(uint32_t flightIndex) const
{
    return _perFlight[flightIndex].visibleInstancesBuf.get();
}

} // namespace ya
