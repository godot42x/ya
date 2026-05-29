#include "PointShadowCullPass.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Render.h"

#include <format>

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
        flight.faceFrustumBuffer.reset();
        flight.drawCommandBuffer.reset();
        flight.visibleInstancesBuf.reset();
        flight.cullDS = nullptr;
    }
    _dsp.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
    _cullDSL.reset();
    _allocatedBucketCount = 0;
    _render               = nullptr;
}

void PointShadowCullPass::ensureCapacity(uint32_t bucketCount)
{
    if (bucketCount == 0 || bucketCount <= _allocatedBucketCount) return;
    _allocatedBucketCount = bucketCount;

    const uint32_t cmdSize      = bucketCount * static_cast<uint32_t>(sizeof(PointShadowIndirectCommand));
    const uint32_t visibleBytes = bucketCount * ShadowConstants::MAX_DRAWS_PER_FACE * static_cast<uint32_t>(sizeof(uint32_t));
    const uint32_t frustumSize  = bucketCount * static_cast<uint32_t>(sizeof(PointShadowFaceFrustum));

    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        auto& flight = _perFlight[i];

        flight.faceFrustumBuffer = IBuffer::create(_render, BufferCreateInfo{
            .label       = std::format("PointShadowCull_Frustum_{}", i),
            .usage       = EBufferUsage::StorageBuffer,
            .size        = frustumSize,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        // Host-visible: CPU writes the static fields once per frame; if the
        // compute path is active it atomically updates instanceCount on top.
        flight.drawCommandBuffer = IBuffer::create(_render, BufferCreateInfo{
            .label       = std::format("PointShadowCull_DrawCmd_{}", i),
            .usage       = EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer,
            .size        = cmdSize,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        flight.visibleInstancesBuf = IBuffer::create(_render, BufferCreateInfo{
            .label       = std::format("PointShadowCull_VisInst_{}", i),
            .usage       = EBufferUsage::StorageBuffer,
            .size        = visibleBytes,
            .memoryUsage = EMemoryUsage::CpuToGpu, // host-visible to allow CPU NoCull writes
        });

        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 1, flight.faceFrustumBuffer.get()),
            IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 2, flight.drawCommandBuffer.get()),
            IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 3, flight.visibleInstancesBuf.get()),
        });
    }
}

void PointShadowCullPass::bindInstanceBuffer(uint32_t flightIndex, IBuffer* instanceBuffer)
{
    if (!instanceBuffer || !_render) return;
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneStorageBuffer(_perFlight[flightIndex].cullDS, 0, instanceBuffer),
    });
}

void PointShadowCullPass::writeDrawCommandTemplate(uint32_t                          flightIndex,
                                                    const PointShadowIndirectCommand* cmds,
                                                    uint32_t                          bucketCount)
{
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
    auto& flight            = _perFlight[flightIndex];
    flight.activeFaceCount  = activeFaceCount;
    flight.activeBatchCount = batchCount;
    flight.instanceCount    = instanceCount;
    if (activeFaceCount == 0 || instanceCount == 0 || batchCount == 0) return;

    flight.faceFrustumBuffer->writeData(faceFrustums, activeFaceCount * sizeof(PointShadowFaceFrustum), 0);
    flight.faceFrustumBuffer->flush();
}

void PointShadowCullPass::dispatch(ICommandBuffer* cmdBuf, uint32_t flightIndex) const
{
    const auto& flight = _perFlight[flightIndex];
    if (flight.activeFaceCount == 0 || flight.instanceCount == 0 || flight.activeBatchCount == 0) return;

    // Previous frame's indirect-read on this buffer must complete before we
    // overwrite instanceCount via compute. (Host writes are auto-visible.)
    cmdBuf->bufferMemoryBarrier(flight.drawCommandBuffer.get(),
                                EPipelineStage::DrawIndirect,
                                EPipelineStage::ComputeShader,
                                EResourceAccess::IndirectCommandRead,
                                EResourceAccess::ShaderWrite | EResourceAccess::ShaderRead);

    cmdBuf->bindComputePipeline(_pipeline.get());
    cmdBuf->bindComputeDescriptorSets(_pipelineLayout.get(), 0, {flight.cullDS});

    PushConstants pc{
        .instanceCount = flight.instanceCount,
        .faceCount     = flight.activeFaceCount,
        .batchCount    = flight.activeBatchCount,
        ._pad          = 0,
    };
    cmdBuf->pushConstants(_pipelineLayout.get(), EShaderStage::Compute, 0, sizeof(PushConstants), &pc);

    const uint32_t groupsX = (flight.instanceCount + ShadowConstants::CULL_WORKGROUP_SIZE - 1) / ShadowConstants::CULL_WORKGROUP_SIZE;
    cmdBuf->dispatch(groupsX, flight.activeFaceCount, 1);

    cmdBuf->bufferMemoryBarrier(flight.drawCommandBuffer.get(),
                                EPipelineStage::ComputeShader,
                                EPipelineStage::DrawIndirect,
                                EResourceAccess::ShaderWrite,
                                EResourceAccess::IndirectCommandRead);
    cmdBuf->bufferMemoryBarrier(flight.visibleInstancesBuf.get(),
                                EPipelineStage::ComputeShader,
                                EPipelineStage::VertexShader,
                                EResourceAccess::ShaderWrite,
                                EResourceAccess::ShaderRead);
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
