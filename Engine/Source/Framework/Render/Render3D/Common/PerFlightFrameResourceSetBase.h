#pragma once

#include "Core/Common/DeferredDeletionQueue.h"
#include "Core/Common/Types.h"
#include "Core/Log.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"
#include "RHI/Render.h"
#include "Render3D/Stage/IRenderStage.h"

#include <algorithm>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace ya
{

/**
 * Shared per-flight resource mechanism for frame resource sets.
 *
 * Covers only the resource/data mechanisms shared by the Forward/Deferred
 * frame resource sets: the per-flight upload arena and the capacity-managed
 * skinning storage buffers (descriptor layout/pool, grow-on-demand capacity,
 * fence-safe retire of replaced buffers). The base deliberately owns nothing
 * of a pipeline's rendering strategy — descriptor sets, `Binding` arrays and
 * pass state stay in the derived class.
 *
 * CRTP contract: the derived class must grant friendship and expose the
 * per-flight skinning slots via
 *   std::array<Binding, MAX_FLIGHTS_IN_FLIGHT>& bindings();
 * where each `Binding` carries `skinningDescriptorSet` (DescriptorSetHandle)
 * and `skinningBuffer` (stdptr<IBuffer>) per flight. Binding field names are
 * verified at instantiation time by the compiler.
 */
template <typename Derived>
class PerFlightFrameResourceSetBase
{
  protected:
    PerFlightFrameResourceSetBase()                             = default;
    ~PerFlightFrameResourceSetBase()                            = default;

    PerFlightFrameResourceSetBase(const PerFlightFrameResourceSetBase&)            = delete;
    PerFlightFrameResourceSetBase& operator=(const PerFlightFrameResourceSetBase&) = delete;

    Derived&       self()       { return static_cast<Derived&>(*this); }
    const Derived& self() const { return static_cast<const Derived&>(*this); }

    /// Creates the skinning descriptor layout/pool and the per-flight upload
    /// arena; grows the initial skinning capacity. Must be called first from
    /// the derived `init()` because it also wires the shared backend pointer.
    void initSkinnedUploadArena(IRender* render,
                                std::string_view pipelineName,
                                std::string_view skinningDSLLabel,
                                int32_t skinningDSLSet,
                                std::string_view uploadArenaLabel);

    /// Releases the shared arena + skinning resources. Old skinning buffers
    /// are retired through DeferredDeletionQueue so in-flight command buffers
    /// recorded before a capacity regrow stay valid until submission.
    void destroySkinnedUploadArena();

    /** Upload the current flight's skinning palettes for the fence-safe flight. */
    bool prepareSkinning(const RenderStageContext& ctx);

    IRender*                           _render           = nullptr;
    std::unique_ptr<FrameUploadArena>  _uploadArena;
    stdptr<IDescriptorSetLayout>       _skinningDSL;
    stdptr<IDescriptorPool>            _skinningDSP;
    uint32_t                           _skinningCapacity = 0;
    std::string                        _resourceTag;

  public:
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkinningDSL() const { return _skinningDSL; }

  protected:
    /// Capacity policy shared by all skinning-backed frame resource sets.
    [[nodiscard]] static std::optional<uint32_t> calculateSkinningCapacity(
        uint32_t currentCapacity,
        uint32_t paletteCount);
    bool ensureSkinningCapacity(uint32_t paletteCount);
};

template <typename Derived>
void PerFlightFrameResourceSetBase<Derived>::initSkinnedUploadArena(
    IRender* render,
    std::string_view pipelineName,
    std::string_view skinningDSLLabel,
    int32_t skinningDSLSet,
    std::string_view uploadArenaLabel)
{
    YA_CORE_ASSERT(render != nullptr, "PerFlightFrameResourceSetBase requires a render backend");
    if (render->getResourceFactory() == nullptr) {
        YA_CORE_ASSERT(false, "PerFlightFrameResourceSetBase requires a resource factory");
        return;
    }

    _render      = render;
    _resourceTag = std::string(pipelineName);

    _skinningDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = std::string(skinningDSLLabel),
            .set      = skinningDSLSet,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });
    YA_CORE_ASSERT(_skinningDSL != nullptr, "{}FrameResourceSet failed to create skinning descriptor layout", _resourceTag);

    _uploadArena = std::make_unique<FrameUploadArena>(
        *render->getResourceFactory(),
        MAX_FLIGHTS_IN_FLIGHT,
        64u * 1024u,
        EBufferUsage::UniformBuffer,
        std::string(uploadArenaLabel));
}

template <typename Derived>
void PerFlightFrameResourceSetBase<Derived>::destroySkinnedUploadArena()
{
    _uploadArena.reset();
    _skinningDSP.reset();
    _skinningDSL.reset();
    _skinningCapacity = 0;
    _render         = nullptr;
    _resourceTag.clear();
}

template <typename Derived>
std::optional<uint32_t> PerFlightFrameResourceSetBase<Derived>::calculateSkinningCapacity(
    uint32_t currentCapacity,
    uint32_t paletteCount)
{
    constexpr uint32_t maxPaletteCount = std::numeric_limits<uint32_t>::max() / sizeof(RenderSkinningPalette);
    const uint32_t requiredCount = std::max(1u, paletteCount);
    if (requiredCount > maxPaletteCount) {
        return std::nullopt;
    }

    uint32_t nextCapacity = currentCapacity == 0 ? 16u : currentCapacity;
    if (nextCapacity > maxPaletteCount) {
        return std::nullopt;
    }
    while (nextCapacity < requiredCount) {
        if (nextCapacity > maxPaletteCount / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }
    return nextCapacity;
}

template <typename Derived>
bool PerFlightFrameResourceSetBase<Derived>::ensureSkinningCapacity(uint32_t paletteCount)
{
    if (_skinningDSP && std::max(1u, paletteCount) <= _skinningCapacity) {
        return true;
    }

    const auto nextCapacity = calculateSkinningCapacity(_skinningCapacity, paletteCount);
    if (!nextCapacity.has_value()) {
        YA_CORE_ERROR("{} skinning palette count {} exceeds buffer size limit", _resourceTag, paletteCount);
        return false;
    }

    auto nextDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = std::format("{}_Skinning_DSP", _resourceTag),
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
        });
    if (!nextDSP) {
        YA_CORE_ERROR("{}FrameResourceSet failed to create skinning descriptor pool", _resourceTag);
        return false;
    }

    const uint32_t bufferSize = *nextCapacity * sizeof(RenderSkinningPalette);
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT> nextBuffers{};
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> nextDescriptorSets{};
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        nextBuffers[flightIndex] = _render->getResourceFactory()->createBuffer(
            BufferCreateInfo{
                .label       = std::format("{}_Skinning_SSBO_{}", _resourceTag, flightIndex),
                .usage       = EBufferUsage::StorageBuffer,
                .size        = bufferSize,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        if (!nextBuffers[flightIndex]) {
            YA_CORE_ERROR("{}FrameResourceSet failed to create skinning buffer for flight {}", _resourceTag, flightIndex);
            return false;
        }

        nextDescriptorSets[flightIndex] = nextDSP->allocateDescriptorSets(_skinningDSL);
        if (!nextDescriptorSets[flightIndex]) {
            YA_CORE_ERROR("{}FrameResourceSet failed to allocate skinning descriptor set for flight {}", _resourceTag, flightIndex);
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
    auto& bindings = self().bindings();
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        oldBuffers[flightIndex] = std::move(bindings[flightIndex].skinningBuffer);
    }

    _skinningDSP      = std::move(nextDSP);
    _skinningCapacity = *nextCapacity;
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        bindings[flightIndex].skinningDescriptorSet = nextDescriptorSets[flightIndex];
        bindings[flightIndex].skinningBuffer        = std::move(nextBuffers[flightIndex]);
    }

    DeferredDeletionQueue::get().retire(std::move(oldDSP));
    for (auto& oldBuffer : oldBuffers) {
        DeferredDeletionQueue::get().retire(std::move(oldBuffer));
    }
    return true;
}

template <typename Derived>
bool PerFlightFrameResourceSetBase<Derived>::prepareSkinning(const RenderStageContext& ctx)
{
    YA_CORE_ASSERT(ctx.frameData != nullptr, "{} skinning prepare requires frame data", _resourceTag);
    const auto& palettes = ctx.frameData->sceneSnapshot.skinningPalettes;
    if (palettes.size() > std::numeric_limits<uint32_t>::max()) {
        YA_CORE_ERROR("{} skinning palette count exceeds uint32 range", _resourceTag);
        return false;
    }
    if (!ensureSkinningCapacity(static_cast<uint32_t>(palettes.size()))) {
        return false;
    }
    if (palettes.empty()) {
        return true;
    }

    auto& buffer = self().bindings()[ctx.flightIndex].skinningBuffer;
    YA_CORE_ASSERT(buffer != nullptr, "{} skinning buffer is missing for flight {}", _resourceTag, ctx.flightIndex);
    const uint32_t byteCount = static_cast<uint32_t>(palettes.size() * sizeof(RenderSkinningPalette));
    return buffer->writeData(palettes.data(), byteCount, 0) && buffer->flush(byteCount, 0);
}

} // namespace ya