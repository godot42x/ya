#include "ForwardFrameResourceSet.h"

#include "Core/Log.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"
#include "Resource/DeferredDeletionQueue.h"

#include <algorithm>
#include <format>
#include <limits>

namespace ya
{

void ForwardFrameResourceSet::init(IRender* render)
{
    destroy();
    YA_CORE_ASSERT(render != nullptr, "ForwardFrameResourceSet requires a render backend");

    _render = render;
    _skinningDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Forward_Skinning_DSL",
            .set      = 5,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });
    YA_CORE_ASSERT(_skinningDSL != nullptr, "ForwardFrameResourceSet failed to create skinning descriptor layout");

    YA_CORE_ASSERT(ensureSkinningCapacity(0), "ForwardFrameResourceSet failed to create initial skinning resources");
}

void ForwardFrameResourceSet::destroy()
{
    _bindings = {};
    _skinningDSP.reset();
    _skinningDSL.reset();
    _skinningCapacity = 0;
    _render = nullptr;
}

bool ForwardFrameResourceSet::ensureSkinningCapacity(uint32_t paletteCount)
{
    constexpr uint32_t maxPaletteCount = std::numeric_limits<uint32_t>::max() / sizeof(RenderSkinningPalette);
    const uint32_t requiredCount = std::max(1u, paletteCount);
    if (requiredCount > maxPaletteCount) {
        YA_CORE_ERROR("Forward skinning palette count {} exceeds buffer size limit", paletteCount);
        return false;
    }

    if (_skinningDSP && requiredCount <= _skinningCapacity) {
        return true;
    }

    uint32_t nextCapacity = _skinningCapacity == 0 ? 16u : _skinningCapacity;
    if (nextCapacity > maxPaletteCount) {
        YA_CORE_ERROR("Forward skinning capacity {} exceeds buffer size limit", nextCapacity);
        return false;
    }
    while (nextCapacity < requiredCount) {
        if (nextCapacity > maxPaletteCount / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }

    auto nextDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "Forward_Skinning_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
        });
    if (!nextDSP) {
        YA_CORE_ERROR("ForwardFrameResourceSet failed to create skinning descriptor pool");
        return false;
    }

    const uint32_t bufferSize = nextCapacity * sizeof(RenderSkinningPalette);
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT> nextBuffers{};
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> nextDescriptorSets{};
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        nextBuffers[flightIndex] = _render->getResourceFactory()->createBuffer(
            BufferCreateInfo{
                .label       = std::format("Forward_Skinning_SSBO_{}", flightIndex),
                .usage       = EBufferUsage::StorageBuffer,
                .size        = bufferSize,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        if (!nextBuffers[flightIndex]) {
            YA_CORE_ERROR("ForwardFrameResourceSet failed to create skinning buffer for flight {}", flightIndex);
            return false;
        }

        nextDescriptorSets[flightIndex] = nextDSP->allocateDescriptorSets(_skinningDSL);
        if (!nextDescriptorSets[flightIndex]) {
            YA_CORE_ERROR("ForwardFrameResourceSet failed to allocate skinning descriptor set for flight {}", flightIndex);
            return false;
        }

        _render->getDescriptorHelper()->updateDescriptorSets(
            {IDescriptorSetHelper::genSingleBufferWrite(
                nextDescriptorSets[flightIndex],
                0,
                EPipelineDescriptorType::StorageBuffer,
                nextBuffers[flightIndex].get())},
            {});
    }

    auto oldDSP = std::move(_skinningDSP);
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT> oldBuffers{};
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        oldBuffers[flightIndex] = std::move(_bindings[flightIndex].skinningBuffer);
    }

    _skinningDSP      = std::move(nextDSP);
    _skinningCapacity = nextCapacity;
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        _bindings[flightIndex].skinningDescriptorSet = nextDescriptorSets[flightIndex];
        _bindings[flightIndex].skinningBuffer        = std::move(nextBuffers[flightIndex]);
    }

    if (!DeferredDeletionQueue::get().isInitialized()) {
        return true;
    }

    DeferredDeletionQueue::get().retireResource(std::move(oldDSP));
    for (auto& oldBuffer : oldBuffers) {
        DeferredDeletionQueue::get().retireResource(std::move(oldBuffer));
    }
    return true;
}

bool ForwardFrameResourceSet::prepareSkinning(const RenderStageContext& ctx)
{
    YA_CORE_ASSERT(ctx.frameData != nullptr, "Forward skinning prepare requires frame data");
    const auto& palettes = ctx.frameData->skinningPalettes;
    if (palettes.size() > std::numeric_limits<uint32_t>::max()) {
        YA_CORE_ERROR("Forward skinning palette count exceeds uint32 range");
        return false;
    }
    if (!ensureSkinningCapacity(static_cast<uint32_t>(palettes.size()))) {
        return false;
    }
    if (palettes.empty()) {
        return true;
    }

    auto& buffer = _bindings[ctx.flightIndex].skinningBuffer;
    YA_CORE_ASSERT(buffer != nullptr, "Forward skinning buffer is missing for flight {}", ctx.flightIndex);
    const uint32_t byteCount = static_cast<uint32_t>(palettes.size() * sizeof(RenderSkinningPalette));
    return buffer->writeData(palettes.data(), byteCount, 0) && buffer->flush(byteCount, 0);
}

const ForwardFrameResourceSet::Binding& ForwardFrameResourceSet::getBinding(uint32_t flightIndex) const
{
    YA_CORE_ASSERT(flightIndex < _bindings.size(), "ForwardFrameResourceSet invalid flight index {}", flightIndex);
    return _bindings[flightIndex];
}

} // namespace ya
