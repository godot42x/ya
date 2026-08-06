#include "PointShadowCullPass.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Resource/DeferredDeletionQueue.h"

#include "Render/Core/Graph/RenderGraphImportUtils.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"
#include "Runtime/Rendering/Common/Shadow/BasicShadowMap/PointShadowBufferUtils.h"

#include <format>
#include <limits>

namespace ya
{

void PointShadowCullPass::init(IRender* render)
{
    _render = render;

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
        flight.faceFrustumUploadBuffer.reset();
        flight.faceFrustumExecBuffer.reset();
        flight.drawCommandUploadBuffer.reset();
        flight.drawCommandExecBuffer.reset();
        flight.visibleInstancesUploadBuffer.reset();
        flight.visibleInstancesExecBuffer.reset();
        flight.instanceBuffer.reset();
        flight.cullDS = nullptr;
        flight.allocatedBucketCount = 0;
    }
    _dsp.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
    _cullDSL.reset();
    _render               = nullptr;
}

bool PointShadowCullPass::ensureCapacity(uint32_t flightIndex, uint32_t bucketCount)
{
    YA_PROFILE_FUNCTION();
    if (flightIndex >= _perFlight.size()) return false;
    auto& flight = _perFlight[flightIndex];
    if (bucketCount == 0) return true;
    if (bucketCount <= flight.allocatedBucketCount &&
        flight.drawCommandUploadBuffer && flight.drawCommandExecBuffer &&
        flight.visibleInstancesUploadBuffer && flight.visibleInstancesExecBuffer &&
        flight.faceFrustumUploadBuffer && flight.faceFrustumExecBuffer) {
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

    auto nextFrustumUpload = createPointShadowBuffer(_render,
                                                     std::format("PointShadowCull_Frustum_{}", flightIndex),
                                                     EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
                                                     static_cast<uint32_t>(frustumBytes),
                                                     EMemoryUsage::CpuToGpu);
    auto nextFrustumExec = createPointShadowBuffer(_render,
                                                   std::format("PointShadowCull_FrustumExec_{}", flightIndex),
                                                   EBufferUsage::StorageBuffer | EBufferUsage::TransferDst,
                                                   static_cast<uint32_t>(frustumBytes),
                                                   EMemoryUsage::GpuOnly);
    auto nextDrawCommandsUpload = createPointShadowBuffer(_render,
                                                          std::format("PointShadowCull_DrawCmd_{}", flightIndex),
                                                          EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
                                                          static_cast<uint32_t>(cmdBytes),
                                                          EMemoryUsage::CpuToGpu);
    auto nextDrawCommandsExec = createPointShadowBuffer(_render,
                                                        std::format("PointShadowCull_DrawCmdExec_{}", flightIndex),
                                                        EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer | EBufferUsage::TransferDst,
                                                        static_cast<uint32_t>(cmdBytes),
                                                        EMemoryUsage::GpuOnly);
    auto nextVisibleInstancesUpload = createPointShadowBuffer(_render,
                                                              std::format("PointShadowCull_VisInst_{}", flightIndex),
                                                              EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
                                                              static_cast<uint32_t>(visibleBytes64),
                                                              EMemoryUsage::CpuToGpu);
    auto nextVisibleInstancesExec = createPointShadowBuffer(_render,
                                                            std::format("PointShadowCull_VisInstExec_{}", flightIndex),
                                                            EBufferUsage::StorageBuffer | EBufferUsage::TransferDst,
                                                            static_cast<uint32_t>(visibleBytes64),
                                                            EMemoryUsage::GpuOnly);
    if (!nextFrustumUpload || !nextFrustumExec ||
        !nextDrawCommandsUpload || !nextDrawCommandsExec ||
        !nextVisibleInstancesUpload || !nextVisibleInstancesExec) {
        YA_CORE_ERROR("PointShadowCullPass failed to allocate buffers for flight {}", flightIndex);
        return false;
    }

    auto oldFrustumUpload = std::move(flight.faceFrustumUploadBuffer);
    auto oldFrustumExec = std::move(flight.faceFrustumExecBuffer);
    auto oldDrawCommandsUpload = std::move(flight.drawCommandUploadBuffer);
    auto oldDrawCommandsExec = std::move(flight.drawCommandExecBuffer);
    auto oldVisibleInstancesUpload = std::move(flight.visibleInstancesUploadBuffer);
    auto oldVisibleInstancesExec = std::move(flight.visibleInstancesExecBuffer);
    flight.faceFrustumUploadBuffer = std::move(nextFrustumUpload);
    flight.faceFrustumExecBuffer = std::move(nextFrustumExec);
    flight.drawCommandUploadBuffer = std::move(nextDrawCommandsUpload);
    flight.drawCommandExecBuffer = std::move(nextDrawCommandsExec);
    flight.visibleInstancesUploadBuffer = std::move(nextVisibleInstancesUpload);
    flight.visibleInstancesExecBuffer = std::move(nextVisibleInstancesExec);
    flight.allocatedBucketCount = bucketCount;

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 1, flight.faceFrustumExecBuffer.get()),
        IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 2, flight.drawCommandExecBuffer.get()),
        IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 3, flight.visibleInstancesExecBuffer.get()),
    });

    if (DeferredDeletionQueue::get().isInitialized()) {
        DeferredDeletionQueue::get().retireResource(std::move(oldFrustumUpload));
        DeferredDeletionQueue::get().retireResource(std::move(oldFrustumExec));
        DeferredDeletionQueue::get().retireResource(std::move(oldDrawCommandsUpload));
        DeferredDeletionQueue::get().retireResource(std::move(oldDrawCommandsExec));
        DeferredDeletionQueue::get().retireResource(std::move(oldVisibleInstancesUpload));
        DeferredDeletionQueue::get().retireResource(std::move(oldVisibleInstancesExec));
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
    if (!flight.drawCommandUploadBuffer) return;
    flight.drawCommandUploadBuffer->writeData(cmds, bucketCount * sizeof(PointShadowIndirectCommand), 0);
    flight.drawCommandUploadBuffer->flush();
}

void PointShadowCullPass::writeVisibleInstances(uint32_t        flightIndex,
                                                const uint32_t* data,
                                                uint32_t        count)
{
    YA_PROFILE_FUNCTION();
    if (count == 0) return;
    auto& flight = _perFlight[flightIndex];
    if (!flight.visibleInstancesUploadBuffer) return;
    flight.visibleInstancesUploadBuffer->writeData(data, count * sizeof(uint32_t), 0);
    flight.visibleInstancesUploadBuffer->flush();
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

    flight.faceFrustumUploadBuffer->writeData(faceFrustums, activeFaceCount * sizeof(PointShadowFaceFrustum), 0);
    flight.faceFrustumUploadBuffer->flush();
}

void PointShadowCullPass::prepareNoCull(uint32_t flightIndex, uint32_t activeFaceCount, uint32_t batchCount)
{
    if (flightIndex >= _perFlight.size()) return;
    auto& flight            = _perFlight[flightIndex];
    flight.activeFaceCount  = activeFaceCount;
    flight.activeBatchCount = batchCount;
    flight.instanceCount    = 0;
}

std::optional<PointShadowCullPass::GraphResources> PointShadowCullPass::appendGraphPass(
    RenderGraph& graph,
    uint32_t flightIndex,
    bool bDispatchCull,
    std::optional<RGPassHandle> dependency)
{
    if (flightIndex >= _perFlight.size()) return std::nullopt;
    const auto& flight = _perFlight[flightIndex];
    if (!flight.instanceBuffer ||
        !flight.drawCommandUploadBuffer || !flight.drawCommandExecBuffer ||
        !flight.visibleInstancesUploadBuffer || !flight.visibleInstancesExecBuffer) {
        return std::nullopt;
    }
    if (bDispatchCull &&
        (!flight.instanceBuffer || !flight.faceFrustumUploadBuffer || !flight.faceFrustumExecBuffer ||
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
        return graph.importBuffer(makeImportedBufferDesc(buffer, label, initialState, usage, finalState));
    };

    const BufferResourceState hostWriteState{
        .stages = EPipelineStage::Host,
        .access = EResourceAccess::HostWrite,
    };
    const BufferResourceState executionInitialState{};
    const auto instanceBuffer = importBuffer(
        graph, flight.instanceBuffer, "PointShadowCull.Instances", EBufferUsage::StorageBuffer, hostWriteState);
    const uint64_t bucketCount = static_cast<uint64_t>(flight.activeFaceCount) * flight.activeBatchCount;
    const uint64_t commandBytes = bucketCount * sizeof(PointShadowIndirectCommand);
    const uint64_t visibleBytes = bucketCount * ShadowConstants::MAX_DRAWS_PER_FACE * sizeof(uint32_t);
    const uint64_t frustumBytes = static_cast<uint64_t>(flight.activeFaceCount) * sizeof(PointShadowFaceFrustum);
    if (bucketCount == 0 || commandBytes > std::numeric_limits<uint32_t>::max() ||
        visibleBytes > std::numeric_limits<uint32_t>::max() ||
        (bDispatchCull && frustumBytes > std::numeric_limits<uint32_t>::max())) {
        return std::nullopt;
    }

    const auto drawCommandUpload = importBuffer(
        graph,
        flight.drawCommandUploadBuffer,
        "PointShadowCull.DrawCommands.Upload",
        EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
        hostWriteState);
    const auto drawCommands = importBuffer(
        graph,
        flight.drawCommandExecBuffer,
        "PointShadowCull.DrawCommands.Exec",
        EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer | EBufferUsage::TransferDst,
        executionInitialState);
    const auto visibleInstancesUpload = importBuffer(
        graph,
        flight.visibleInstancesUploadBuffer,
        "PointShadowCull.VisibleInstances.Upload",
        EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
        hostWriteState);
    const auto visibleInstances = importBuffer(
        graph,
        flight.visibleInstancesExecBuffer,
        "PointShadowCull.VisibleInstances.Exec",
        EBufferUsage::StorageBuffer | EBufferUsage::TransferDst,
        executionInitialState);

    GraphResources resources{
        .instanceData     = instanceBuffer,
        .drawCommands     = drawCommands,
        .visibleInstances = visibleInstances,
    };

    std::optional<RGBufferHandle> frustumBuffer;
    std::optional<RGBufferHandle> frustumUpload;
    if (bDispatchCull) {
        frustumUpload = importBuffer(
            graph,
            flight.faceFrustumUploadBuffer,
            "PointShadowCull.Frustums.Upload",
            EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
            hostWriteState);
        frustumBuffer = importBuffer(
            graph,
            flight.faceFrustumExecBuffer,
            "PointShadowCull.Frustums.Exec",
            EBufferUsage::StorageBuffer | EBufferUsage::TransferDst,
            executionInitialState);
    }
    const auto appendCopy = [&graph](std::string_view label,
                                     std::vector<RGBufferCopyRegion> copies,
                                     std::optional<RGPassHandle> dependency) {
        return addBufferCopyPass(graph, RGBufferCopyParams{
            .label      = label,
            .copies     = std::move(copies),
            .dependency = dependency,
        });
    };

    std::vector<RGBufferCopyRegion> uploadCopies{
        RGBufferCopyRegion{
            .source      = drawCommandUpload,
            .destination = drawCommands,
            .size        = commandBytes,
        },
    };
    if (!bDispatchCull) {
        uploadCopies.push_back(RGBufferCopyRegion{
            .source      = visibleInstancesUpload,
            .destination = visibleInstances,
            .size        = visibleBytes,
        });
    } else {
        uploadCopies.push_back(RGBufferCopyRegion{
            .source      = *frustumUpload,
            .destination = *frustumBuffer,
            .size        = frustumBytes,
        });
    }
    const auto uploadPass = appendCopy("Point Shadow Cull Upload", std::move(uploadCopies), dependency);
    const auto frustumHandle = frustumBuffer.value_or(RGBufferHandle{});

    if (!bDispatchCull) {
        resources.cullPass = uploadPass;
        return resources;
    }

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
    return _perFlight[flightIndex].drawCommandExecBuffer.get();
}

IBuffer* PointShadowCullPass::getVisibleInstancesBuffer(uint32_t flightIndex) const
{
    return _perFlight[flightIndex].visibleInstancesExecBuffer.get();
}

} // namespace ya
