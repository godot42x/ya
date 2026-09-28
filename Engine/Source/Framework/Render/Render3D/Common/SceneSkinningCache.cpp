#include "Render3D/Common/SceneSkinningCache.h"

#include "Core/Common/DeferredDeletionQueue.h"
#include "Core/Log.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "Render3D/Common/SceneFamilyResources.h"
#include "Render3D/RenderFrameData.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace ya
{

SceneSkinningCache::~SceneSkinningCache() = default;

stdptr<IBuffer> SceneSkinningCache::resolve(Scene*                       scene,
                                             uint64_t                     sceneRevision,
                                             const RenderSkinningPalette* palettes,
                                             uint32_t                     paletteCount,
                                             uint32_t                     flightIndex,
                                             IRenderResourceFactory&      factory,
                                             std::string_view             label)
{
    if (flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        YA_CORE_ERROR("SceneSkinningCache::resolve flight {} out of range", flightIndex);
        return nullptr;
    }

    const SceneSkinningCacheKey key{.scene = scene, .sceneRevision = sceneRevision};
    auto [it, _] = _entries.try_emplace(key, Entry{.scene = scene, .sceneRevision = sceneRevision});
    Entry&      entry = it->second;
    FlightSlot& slot  = entry.flights[flightIndex];

    const uint32_t requiredCount = std::max(1u, paletteCount);
    if (!slot.buffer || slot.capacity < requiredCount) {
        const auto nextCapacity = calculateSceneFamilySkinningCapacity(slot.capacity, paletteCount);
        if (!nextCapacity.has_value()) {
            YA_CORE_ERROR("SceneSkinningCache palette count {} exceeds buffer size limit", paletteCount);
            return nullptr;
        }

        auto nextBuffer = factory.createBuffer(BufferCreateInfo{
            .label       = std::format("{}_Skinning_SSBO.flight{}", label, flightIndex),
            .usage       = EBufferUsage::StorageBuffer,
            .size        = static_cast<uint32_t>(*nextCapacity * sizeof(RenderSkinningPalette)),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        if (!nextBuffer) {
            YA_CORE_ERROR("SceneSkinningCache failed to create skinning buffer");
            return nullptr;
        }

        if (slot.buffer) {
            DeferredDeletionQueue::get().retire(std::move(slot.buffer));
        }
        slot.buffer   = std::move(nextBuffer);
        slot.capacity = *nextCapacity;
        slot.uploadedBytes.clear();
    }

    const size_t   byteCount = static_cast<size_t>(paletteCount) * sizeof(RenderSkinningPalette);
    const uint8_t* bytes     = reinterpret_cast<const uint8_t*>(palettes);
    const bool     bSame =
        slot.uploadedBytes.size() == byteCount &&
        (byteCount == 0 || std::memcmp(slot.uploadedBytes.data(), bytes, byteCount) == 0);
    if (!bSame) {
        if (byteCount > 0 &&
            (!slot.buffer->writeData(bytes, static_cast<uint32_t>(byteCount), 0) ||
             !slot.buffer->flush(static_cast<uint32_t>(byteCount), 0))) {
            YA_CORE_ERROR("SceneSkinningCache failed to upload skinning palettes");
            return nullptr;
        }
        slot.uploadedBytes.assign(bytes, bytes + byteCount);
    }
    return slot.buffer;
}

void SceneSkinningCache::dropScenesAbsentFrom(std::span<Scene* const> scenes)
{
    for (auto it = _entries.begin(); it != _entries.end();) {
        if (std::find(scenes.begin(), scenes.end(), it->first.scene) != scenes.end()) {
            ++it;
            continue;
        }
        for (auto& slot : it->second.flights) {
            if (slot.buffer) {
                DeferredDeletionQueue::get().retire(std::move(slot.buffer));
            }
            slot.capacity = 0;
            slot.uploadedBytes.clear();
        }
        it = _entries.erase(it);
    }
}

void SceneSkinningCache::clear()
{
    for (auto& [_, entry] : _entries) {
        for (auto& slot : entry.flights) {
            if (slot.buffer) {
                DeferredDeletionQueue::get().retire(std::move(slot.buffer));
            }
        }
    }
    _entries.clear();
}

} // namespace ya
