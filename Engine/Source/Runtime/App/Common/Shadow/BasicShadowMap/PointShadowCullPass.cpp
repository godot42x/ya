#include "PointShadowCullPass.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Render.h"

#include <format>

namespace ya
{

void PointShadowCullPass::init(IRender* render)
{
    _render = render;

    // DSL: set 0 with 4 storage buffers (instances, frustums, drawCommands, drawCounts)
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
    }), "Failed to create point shadow cull pipeline");

    // Descriptor pool: 4 storage buffers × MAX_FLIGHTS_IN_FLIGHT sets
    _dsp = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
        .label     = "PointShadowCull_DSP",
        .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
        .poolSizes = {
            {.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT * 4},
        },
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
        flight.drawCountBuffer.reset();
        flight.cullDS = nullptr;
    }
    _dsp.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
    _cullDSL.reset();
    _allocatedFaceCount = 0;
    _render = nullptr;
}

void PointShadowCullPass::ensureBufferCapacity(uint32_t faceCount)
{
    if (faceCount <= _allocatedFaceCount) return;

    _allocatedFaceCount = faceCount;

    const uint32_t drawCmdSize   = ShadowConstants::MAX_DRAWS_PER_FACE * _allocatedFaceCount * sizeof(PointShadowIndirectCommand);
    const uint32_t drawCountSize = _allocatedFaceCount * sizeof(uint32_t);
    const uint32_t frustumSize   = _allocatedFaceCount * sizeof(PointShadowFaceFrustum);

    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        auto& flight = _perFlight[i];

        flight.faceFrustumBuffer = IBuffer::create(_render, BufferCreateInfo{
            .label       = std::format("PointShadowCull_Frustum_{}", i),
            .usage       = EBufferUsage::StorageBuffer,
            .size        = frustumSize,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        flight.drawCommandBuffer = IBuffer::create(_render, BufferCreateInfo{
            .label       = std::format("PointShadowCull_DrawCmd_{}", i),
            .usage       = EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer,
            .size        = drawCmdSize,
            .memoryUsage = EMemoryUsage::Auto,
        });
        flight.drawCountBuffer = IBuffer::create(_render, BufferCreateInfo{
            .label       = std::format("PointShadowCull_DrawCount_{}", i),
            .usage       = EBufferUsage::StorageBuffer | EBufferUsage::IndirectBuffer | EBufferUsage::TransferDst,
            .size        = drawCountSize,
            .memoryUsage = EMemoryUsage::Auto,
        });

        // Update descriptor set bindings
        _render->getDescriptorHelper()->updateDescriptorSets({
            // binding 0 = instances (will be updated in prepare when instance buffer changes)
            // For now write placeholders; the instance buffer is owned by PointShadowPass
            IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 1, flight.faceFrustumBuffer.get()),
            IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 2, flight.drawCommandBuffer.get()),
            IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 3, flight.drawCountBuffer.get()),
        });
    }
}

void PointShadowCullPass::prepare(uint32_t                      flightIndex,
                                   const PointShadowFaceFrustum* faceFrustums,
                                   uint32_t                      activeFaceCount,
                                   uint32_t                      instanceCount)
{
    if (activeFaceCount == 0 || instanceCount == 0) return;

    ensureBufferCapacity(activeFaceCount);

    auto& flight = _perFlight[flightIndex];
    flight.activeFaceCount = activeFaceCount;
    flight.instanceCount   = instanceCount;

    // Upload frustum data
    flight.faceFrustumBuffer->writeData(faceFrustums, activeFaceCount * sizeof(PointShadowFaceFrustum), 0);
    flight.faceFrustumBuffer->flush();
}

void PointShadowCullPass::dispatch(ICommandBuffer* cmdBuf, uint32_t flightIndex) const
{
    const auto& flight = _perFlight[flightIndex];
    if (flight.activeFaceCount == 0 || flight.instanceCount == 0) return;

    // Zero the draw count buffer
    const uint64_t countSize = static_cast<uint64_t>(flight.activeFaceCount) * sizeof(uint32_t);
    cmdBuf->fillBuffer(flight.drawCountBuffer.get(), 0, countSize, 0);

    // Barrier: transfer → compute
    cmdBuf->bufferMemoryBarrier(flight.drawCountBuffer.get(),
                                EPipelineStage::Transfer,
                                EPipelineStage::ComputeShader,
                                EResourceAccess::TransferWrite,
                                EResourceAccess::ShaderWrite | EResourceAccess::ShaderRead);

    // Dispatch compute
    cmdBuf->bindComputePipeline(_pipeline.get());
    cmdBuf->bindComputeDescriptorSets(_pipelineLayout.get(), 0, {flight.cullDS});

    PushConstants pc{.instanceCount = flight.instanceCount, .faceCount = flight.activeFaceCount};
    cmdBuf->pushConstants(_pipelineLayout.get(), EShaderStage::Compute, 0, sizeof(PushConstants), &pc);

    const uint32_t groupsX = (flight.instanceCount + ShadowConstants::CULL_WORKGROUP_SIZE - 1) / ShadowConstants::CULL_WORKGROUP_SIZE;
    cmdBuf->dispatch(groupsX, flight.activeFaceCount, 1);

    // Barrier: compute write → indirect read + vertex shader read
    cmdBuf->bufferMemoryBarrier(flight.drawCommandBuffer.get(),
                                EPipelineStage::ComputeShader,
                                EPipelineStage::DrawIndirect,
                                EResourceAccess::ShaderWrite,
                                EResourceAccess::IndirectCommandRead);
    cmdBuf->bufferMemoryBarrier(flight.drawCountBuffer.get(),
                                EPipelineStage::ComputeShader,
                                EPipelineStage::DrawIndirect,
                                EResourceAccess::ShaderWrite,
                                EResourceAccess::IndirectCommandRead);
}

IBuffer* PointShadowCullPass::getDrawCommandBuffer(uint32_t flightIndex) const
{
    return _perFlight[flightIndex].drawCommandBuffer.get();
}

IBuffer* PointShadowCullPass::getDrawCountBuffer(uint32_t flightIndex) const
{
    return _perFlight[flightIndex].drawCountBuffer.get();
}

uint32_t PointShadowCullPass::getActiveFaceCount(uint32_t flightIndex) const
{
    return _perFlight[flightIndex].activeFaceCount;
}

void PointShadowCullPass::bindInstanceBuffer(uint32_t flightIndex, IBuffer* instanceBuffer)
{
    if (!instanceBuffer || !_render) return;
    auto& flight = _perFlight[flightIndex];
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneStorageBuffer(flight.cullDS, 0, instanceBuffer),
    });
}

} // namespace ya
