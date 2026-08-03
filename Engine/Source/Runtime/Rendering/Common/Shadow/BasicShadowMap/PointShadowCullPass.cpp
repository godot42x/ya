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
            .usage       = EBufferUsage::StorageBuffer,
            .size        = static_cast<uint32_t>(frustumBytes),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
    // Host-visible: CPU writes the static fields once per frame; if the
    // compute path is active it atomically updates instanceCount on top.
    auto nextDrawCommands = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
            .label       = std::format("PointShadowCull_DrawCmd_{}", flightIndex),
            .usage       = EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer,
            .size        = static_cast<uint32_t>(cmdBytes),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
    auto nextVisibleInstances = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
            .label       = std::format("PointShadowCull_VisInst_{}", flightIndex),
            .usage       = EBufferUsage::StorageBuffer,
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
    const BufferResourceState indirectReadState{
        .stages = EPipelineStage::DrawIndirect,
        .access = EResourceAccess::IndirectCommandRead,
    };
    const BufferResourceState shaderReadState{
        .stages = EPipelineStage::AllCommands,
        .access = EResourceAccess::ShaderRead,
    };
    const auto drawCommandBuffer = importBuffer(
        graph,
        flight.drawCommandBuffer,
        "PointShadowCull.DrawCommands",
        EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer,
        hostWriteState,
        indirectReadState);
    const auto visibleInstances = importBuffer(
        graph,
        flight.visibleInstancesBuf,
        "PointShadowCull.VisibleInstances",
        EBufferUsage::StorageBuffer,
        bDispatchCull ? BufferResourceState{} : hostWriteState,
        shaderReadState);

    GraphResources resources{
        .drawCommands     = drawCommandBuffer,
        .visibleInstances = visibleInstances,
    };
    if (!bDispatchCull) return resources;

    const auto instanceBuffer = importBuffer(
        graph, flight.instanceBuffer, "PointShadowCull.Instances", EBufferUsage::StorageBuffer, hostWriteState);
    const auto frustumBuffer = importBuffer(
        graph, flight.faceFrustumBuffer, "PointShadowCull.Frustums", EBufferUsage::StorageBuffer, hostWriteState);

    PushConstants pc{
        .instanceCount = flight.instanceCount,
        .faceCount     = flight.activeFaceCount,
        .batchCount    = flight.activeBatchCount,
        ._pad          = 0,
    };
    const uint32_t groupsX = (flight.instanceCount + ShadowConstants::CULL_WORKGROUP_SIZE - 1) / ShadowConstants::CULL_WORKGROUP_SIZE;
    resources.cullPass = graph.addPass(
        "Point Shadow Cull",
        [instanceBuffer, frustumBuffer, drawCommandBuffer, visibleInstances, dependency](RGPassBuilder& pass) {
            if (dependency.has_value()) pass.dependsOn(*dependency);
            pass.declareCompute();
            pass.storageRead(instanceBuffer);
            pass.storageRead(frustumBuffer);
            pass.storageReadWrite(drawCommandBuffer);
            pass.storageWrite(visibleInstances);
        },
        [this, cullDS = flight.cullDS, pc, groupsX, faceCount = flight.activeFaceCount](RGRenderContext& ctx) {
            YA_PROFILE_SCOPE("PointShadowPass::CullDispatch");
            YA_PERF_SCOPE(perf::sample::shadowPointCull(), perf::metric::cpuTimeMs(), perf::domain::render());
            auto& commandBuffer = ctx.getCommandBuffer();
            commandBuffer.bindComputePipeline(_pipeline.get());
            commandBuffer.bindComputeDescriptorSets(_pipelineLayout.get(), 0, {cullDS});
            commandBuffer.pushConstants(_pipelineLayout.get(), EShaderStage::Compute, 0, sizeof(PushConstants), &pc);
            commandBuffer.dispatch(groupsX, faceCount, 1);
        });
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
