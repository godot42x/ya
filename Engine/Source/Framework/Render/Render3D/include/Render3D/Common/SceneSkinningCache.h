#pragma once

#include "Core/Common/Types.h"
#include "RHI/RenderDefines.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ya
{

struct IBuffer;
struct IRenderResourceFactory;
struct RenderSkinningPalette;
struct Scene;

struct SceneSkinningCacheKey
{
    Scene*   scene         = nullptr;
    uint64_t sceneRevision = 0;

    bool operator==(const SceneSkinningCacheKey&) const = default;
};

struct SceneSkinningCacheKeyHash
{
    size_t operator()(const SceneSkinningCacheKey& key) const
    {
        size_t hash = std::hash<const Scene*>{}(key.scene);
        hash ^= std::hash<uint64_t>{}(key.sceneRevision) + static_cast<size_t>(0x9e3779b9u) +
                (hash << 6u) + (hash >> 2u);
        return hash;
    }
};

class SceneSkinningCache
{
    struct FlightSlot
    {
        stdptr<IBuffer>      buffer;
        uint32_t             capacity = 0;
        std::vector<uint8_t> uploadedBytes;
    };

    struct Entry
    {
        Scene*                                        scene         = nullptr;
        uint64_t                                      sceneRevision = 0;
        std::array<FlightSlot, MAX_FLIGHTS_IN_FLIGHT> flights{};
    };

    std::unordered_map<SceneSkinningCacheKey, Entry, SceneSkinningCacheKeyHash> _entries;

  public:
    SceneSkinningCache()  = default;
    ~SceneSkinningCache();

    SceneSkinningCache(const SceneSkinningCache&)            = delete;
    SceneSkinningCache& operator=(const SceneSkinningCache&) = delete;

    [[nodiscard]] stdptr<IBuffer> resolve(Scene*                       scene,
                                          uint64_t                     sceneRevision,
                                          const RenderSkinningPalette* palettes,
                                          uint32_t                     paletteCount,
                                          uint32_t                     flightIndex,
                                          IRenderResourceFactory&      factory,
                                          std::string_view             label);

    void dropScenesAbsentFrom(std::span<Scene* const> scenes);

    void clear();

    [[nodiscard]] size_t entryCount() const { return _entries.size(); }
};

} // namespace ya
