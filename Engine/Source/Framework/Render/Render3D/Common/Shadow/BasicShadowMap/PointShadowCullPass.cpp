#include "Render3D/Common/Shadow/BasicShadowMap/PointShadowCullPass.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Common/DeferredDeletionQueue.h"

#include "Graph/RenderGraphImportUtils.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"
#include "Render3D/Common/Shadow/BasicShadowMap/PointShadowBufferUtils.h"

#include <format>
#include <limits>
#include <vector>

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
}

void PointShadowCullPass::destroy()
{
    _pipeline.reset();
    _pipelineLayout.reset();
    _cullDSL.reset();
    _render = nullptr;
}

bool PointShadowCullPass::ensureCapacity(PointShadowIndirectResources& packet, uint32_t bucketCount)
{
    YA_PROFILE_FUNCTION();
    if (bucketCount == 0) return true;
    if (bucketCount <= packet.allocatedBucketCount &&
        packet.drawCommandUploadBuffer && packet.drawCommandExecBuffer &&
        packet.visibleInstancesUploadBuffer && packet.visibleInstancesExecBuffer &&
        packet.faceFrustumUploadBuffer && packet.faceFrustumExecBuffer) {
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
                                                     "PointShadowCull_Frustum",
                                                     EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
                                                     static_cast<uint32_t>(frustumBytes),
                                                     EMemoryUsage::CpuToGpu);
    auto nextFrustumExec = createPointShadowBuffer(_render,
                                                   "PointShadowCull_FrustumExec",
                                                   EBufferUsage::StorageBuffer | EBufferUsage::TransferDst,
                                                   static_cast<uint32_t>(frustumBytes),
                                                   EMemoryUsage::GpuOnly);
    auto nextDrawCommandsUpload = createPointShadowBuffer(_render,
                                                          "PointShadowCull_DrawCmd",
                                                          EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
                                                          static_cast<uint32_t>(cmdBytes),
                                                          EMemoryUsage::CpuToGpu);
    auto nextDrawCommandsExec = createPointShadowBuffer(_render,
                                                        "PointShadowCull_DrawCmdExec",
                                                        EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer | EBufferUsage::TransferDst,
                                                        static_cast<uint32_t>(cmdBytes),
                                                        EMemoryUsage::GpuOnly);
    auto nextVisibleInstancesUpload = createPointShadowBuffer(_render,
                                                              "PointShadowCull_VisInst",
                                                              EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
                                                              static_cast<uint32_t>(visibleBytes64),
                                                              EMemoryUsage::CpuToGpu);
    auto nextVisibleInstancesExec = createPointShadowBuffer(_render,
                                                            "PointShadowCull_VisInstExec",
                                                            EBufferUsage::StorageBuffer | EBufferUsage::TransferDst,
                                                            static_cast<uint32_t>(visibleBytes64),
                                                            EMemoryUsage::GpuOnly);
    if (!nextFrustumUpload || !nextFrustumExec ||
        !nextDrawCommandsUpload || !nextDrawCommandsExec ||
        !nextVisibleInstancesUpload || !nextVisibleInstancesExec) {
        YA_CORE_ERROR("PointShadowCullPass failed to allocate View cull buffers");
        return false;
    }

    auto oldFrustumUpload = std::move(packet.faceFrustumUploadBuffer);
    auto oldFrustumExec = std::move(packet.faceFrustumExecBuffer);
    auto oldDrawCommandsUpload = std::move(packet.drawCommandUploadBuffer);
    auto oldDrawCommandsExec = std::move(packet.drawCommandExecBuffer);
    auto oldVisibleInstancesUpload = std::move(packet.visibleInstancesUploadBuffer);
    auto oldVisibleInstancesExec = std::move(packet.visibleInstancesExecBuffer);
    packet.faceFrustumUploadBuffer = std::move(nextFrustumUpload);
    packet.faceFrustumExecBuffer = std::move(nextFrustumExec);
    packet.drawCommandUploadBuffer = std::move(nextDrawCommandsUpload);
    packet.drawCommandExecBuffer = std::move(nextDrawCommandsExec);
    packet.visibleInstancesUploadBuffer = std::move(nextVisibleInstancesUpload);
    packet.visibleInstancesExecBuffer = std::move(nextVisibleInstancesExec);
    packet.allocatedBucketCount = bucketCount;

    DeferredDeletionQueue::get().retire(std::move(oldFrustumUpload));
    DeferredDeletionQueue::get().retire(std::move(oldFrustumExec));
    DeferredDeletionQueue::get().retire(std::move(oldDrawCommandsUpload));
    DeferredDeletionQueue::get().retire(std::move(oldDrawCommandsExec));
    DeferredDeletionQueue::get().retire(std::move(oldVisibleInstancesUpload));
    DeferredDeletionQueue::get().retire(std::move(oldVisibleInstancesExec));
    return true;
}

void PointShadowCullPass::bindInstanceBuffer(PointShadowIndirectResources& packet, const stdptr<IBuffer>& instanceBuffer)
{
    if (!instanceBuffer || !_render) return;
    packet.cullInstanceBuffer = instanceBuffer;
    packet.instanceBuffer     = instanceBuffer;
    if (!packet.cullDS) {
        return;
    }
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneStorageBuffer(packet.cullDS, 0, instanceBuffer.get()),
    });
}

void PointShadowCullPass::updateCullDescriptors(PointShadowIndirectResources& packet)
{
    if (!_render || !packet.cullDS) {
        return;
    }
    std::vector<WriteDescriptorSet> writes;
    if (packet.cullInstanceBuffer) {
        writes.push_back(IDescriptorSetHelper::writeOneStorageBuffer(packet.cullDS, 0, packet.cullInstanceBuffer.get()));
    }
    if (packet.faceFrustumExecBuffer) {
        writes.push_back(IDescriptorSetHelper::writeOneStorageBuffer(packet.cullDS, 1, packet.faceFrustumExecBuffer.get()));
    }
    if (packet.drawCommandExecBuffer) {
        writes.push_back(IDescriptorSetHelper::writeOneStorageBuffer(packet.cullDS, 2, packet.drawCommandExecBuffer.get()));
    }
    if (packet.visibleInstancesExecBuffer) {
        writes.push_back(IDescriptorSetHelper::writeOneStorageBuffer(packet.cullDS, 3, packet.visibleInstancesExecBuffer.get()));
    }
    if (!writes.empty()) {
        _render->getDescriptorHelper()->updateDescriptorSets(writes);
    }
}

void PointShadowCullPass::writeDrawCommandTemplate(PointShadowIndirectResources&     packet,
                                                   const PointShadowIndirectCommand* cmds,
                                                   uint32_t                          bucketCount)
{
    YA_PROFILE_FUNCTION();
    if (bucketCount == 0) return;
    if (!packet.drawCommandUploadBuffer) return;
    packet.drawCommandUploadBuffer->writeData(cmds, bucketCount * sizeof(PointShadowIndirectCommand), 0);
    packet.drawCommandUploadBuffer->flush();
}

void PointShadowCullPass::writeVisibleInstances(PointShadowIndirectResources& packet,
                                                const uint32_t*               data,
                                                uint32_t                      count)
{
    YA_PROFILE_FUNCTION();
    if (count == 0) return;
    if (!packet.visibleInstancesUploadBuffer) return;
    packet.visibleInstancesUploadBuffer->writeData(data, count * sizeof(uint32_t), 0);
    packet.visibleInstancesUploadBuffer->flush();
}

void PointShadowCullPass::prepareCompute(PointShadowIndirectResources& packet,
                                         const PointShadowFaceFrustum* faceFrustums,
                                         uint32_t                      activeFaceCount,
                                         uint32_t                      instanceCount,
                                         uint32_t                      batchCount)
{
    YA_PROFILE_FUNCTION();
    packet.activeFaceCount  = activeFaceCount;
    packet.activeBatchCount = batchCount;
    packet.cullInstanceCount = instanceCount;
    if (activeFaceCount == 0 || instanceCount == 0 || batchCount == 0) return;

    packet.faceFrustumUploadBuffer->writeData(faceFrustums, activeFaceCount * sizeof(PointShadowFaceFrustum), 0);
    packet.faceFrustumUploadBuffer->flush();
}

void PointShadowCullPass::prepareNoCull(PointShadowIndirectResources& packet, uint32_t activeFaceCount, uint32_t batchCount)
{
    packet.activeFaceCount   = activeFaceCount;
    packet.activeBatchCount  = batchCount;
    packet.cullInstanceCount = 0;
}

std::optional<PointShadowCullPass::GraphResources> PointShadowCullPass::appendGraphPass(
    RenderGraph&                  graph,
    PointShadowIndirectResources& packet,
    bool                          bDispatchCull,
    std::optional<RGPassHandle>   dependency)
{
    if (!packet.instanceBuffer ||
        !packet.drawCommandUploadBuffer || !packet.drawCommandExecBuffer ||
        !packet.visibleInstancesUploadBuffer || !packet.visibleInstancesExecBuffer) {
        return std::nullopt;
    }
    if (bDispatchCull &&
        (!packet.instanceBuffer || !packet.faceFrustumUploadBuffer || !packet.faceFrustumExecBuffer ||
         packet.activeFaceCount == 0 || packet.cullInstanceCount == 0 || packet.activeBatchCount == 0)) {
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
        graph, packet.instanceBuffer, "PointShadowCull.Instances", EBufferUsage::StorageBuffer, hostWriteState);
    const uint64_t bucketCount = static_cast<uint64_t>(packet.activeFaceCount) * packet.activeBatchCount;
    const uint64_t commandBytes = bucketCount * sizeof(PointShadowIndirectCommand);
    const uint64_t visibleBytes = bucketCount * ShadowConstants::MAX_DRAWS_PER_FACE * sizeof(uint32_t);
    const uint64_t frustumBytes = static_cast<uint64_t>(packet.activeFaceCount) * sizeof(PointShadowFaceFrustum);
    if (bucketCount == 0 || commandBytes > std::numeric_limits<uint32_t>::max() ||
        visibleBytes > std::numeric_limits<uint32_t>::max() ||
        (bDispatchCull && frustumBytes > std::numeric_limits<uint32_t>::max())) {
        return std::nullopt;
    }

    const auto drawCommandUpload = importBuffer(
        graph,
        packet.drawCommandUploadBuffer,
        "PointShadowCull.DrawCommands.Upload",
        EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
        hostWriteState);
    const auto drawCommands = importBuffer(
        graph,
        packet.drawCommandExecBuffer,
        "PointShadowCull.DrawCommands.Exec",
        EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer | EBufferUsage::TransferDst,
        executionInitialState);
    const auto visibleInstancesUpload = importBuffer(
        graph,
        packet.visibleInstancesUploadBuffer,
        "PointShadowCull.VisibleInstances.Upload",
        EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
        hostWriteState);
    const auto visibleInstances = importBuffer(
        graph,
        packet.visibleInstancesExecBuffer,
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
            packet.faceFrustumUploadBuffer,
            "PointShadowCull.Frustums.Upload",
            EBufferUsage::StorageBuffer | EBufferUsage::TransferSrc,
            hostWriteState);
        frustumBuffer = importBuffer(
            graph,
            packet.faceFrustumExecBuffer,
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
        .instanceCount = packet.cullInstanceCount,
        .faceCount     = packet.activeFaceCount,
        .batchCount    = packet.activeBatchCount,
        ._pad          = 0,
    };
    const uint32_t groupsX = (packet.cullInstanceCount + ShadowConstants::CULL_WORKGROUP_SIZE - 1) / ShadowConstants::CULL_WORKGROUP_SIZE;
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
        [this, cullDS = packet.cullDS, instanceBuffer, frustumHandle, drawCommands, visibleInstances,
         pc, groupsX, faceCount = packet.activeFaceCount](RGRenderContext& ctx) {
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

} // namespace ya
