#pragma once

#include "Core/Common/Types.h"
#include "Core/Log.h"
#include "RHI/Core/Texture.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ya
{

/// Identity of one texture-table entry.
///
/// World sprites key by the binding's image view and sampler, so two views of
/// the same texture with different samplers do not share a slot. Screen draws
/// key by `Texture*` (`fromTexture`); a null texture is expressed by passing
/// that key as the white key, which stays in slot 0. The table itself is CPU
/// state: it does not allocate GPU objects.
struct TextureTableKey
{
    const void* identity  = nullptr;
    uintptr_t   imageView = 0;
    uintptr_t   sampler   = 0;

    [[nodiscard]] static TextureTableKey fromBinding(const TextureBinding& binding)
    {
        return TextureTableKey{
            .identity  = binding.getTexture(),
            .imageView = static_cast<uintptr_t>(binding.getImageViewHandle()),
            .sampler   = static_cast<uintptr_t>(binding.getSamplerHandle()),
        };
    }

    [[nodiscard]] static TextureTableKey fromTexture(const Texture* texture)
    {
        return TextureTableKey{.identity = texture};
    }

    friend bool operator==(const TextureTableKey& lhs, const TextureTableKey& rhs) = default;
};

/// One draw that shares a pipeline and one texture table.
///
/// `slots[0]` is the white sentinel the caller supplied. Real textures occupy
/// `1 .. slots.size()-1` in the order they were first seen inside this batch.
template <typename SlotValue>
struct TextureDrawBatch
{
    uint32_t                first = 0;
    uint32_t                count = 0;
    std::vector<SlotValue>  slots;
};

template <typename Instance, typename SlotValue>
struct InstancedDrawPlan
{
    std::vector<Instance>                  instances;
    std::vector<TextureDrawBatch<SlotValue>> batches;
    /// Key equality tests performed while placing this plan. A direct map stays
    /// proportional to the sprite count; a linear scan of the live table does not.
    uint64_t keyComparisons = 0;
};

/// Epoch-stamped direct map from a texture key to a slot in the current batch.
///
/// Slot 0 is reserved for `white` and is never assigned to another key. A key
/// that does not fit returns nullopt; the caller starts a new batch (which
/// bumps the epoch) and asks again, so the same key is remapped from slot 1.
/// Bumping the epoch drops the previous batch's slots without clearing the map.
class TextureTableCursor
{
  public:
    static constexpr uint32_t kDefaultCapacity = 16;

  private:
    struct Entry
    {
        TextureTableKey key{};
        uint32_t        epoch = 0;
        uint32_t        slot  = 0;
    };

    TextureTableKey    _white{};
    uint32_t           _capacity    = kDefaultCapacity;
    uint32_t           _epoch       = 1;
    uint32_t           _filled      = 1;
    uint64_t           _comparisons = 0;
    std::vector<Entry> _entries;

  public:
    explicit TextureTableCursor(TextureTableKey white = {}, uint32_t capacity = kDefaultCapacity)
        : _white(white),
          _capacity(capacity < 1u ? 1u : capacity),
          _entries(128)
    {
    }

    void beginBatch()
    {
        ++_epoch;
        if (_epoch == 0) {
            _entries.assign(_entries.size(), {});
            _epoch = 1;
        }
        _filled = 1;
    }

    [[nodiscard]] uint64_t comparisons() const { return _comparisons; }
    [[nodiscard]] uint32_t filled() const { return _filled; }

    /// Slot for `key` in the current batch, or nullopt when a new key does not fit.
    [[nodiscard]] std::optional<uint32_t> tryAdd(const TextureTableKey& key)
    {
        return tryAdd(key, false);
    }

    [[nodiscard]] std::optional<uint32_t> tryAdd(const TextureTableKey& key, bool bGrew)
    {
        if (key == _white) {
            ++_comparisons;
            return 0u;
        }

        const uint32_t mask  = static_cast<uint32_t>(_entries.size() - 1u);
        uint32_t       index = static_cast<uint32_t>(hash(key) & mask);
        for (uint32_t probe = 0; probe < _entries.size(); ++probe) {
            Entry& entry = _entries[index];
            if (entry.epoch == 0) {
                return insertNew(entry, key);
            }
            ++_comparisons;
            if (entry.key == key) {
                if (entry.epoch == _epoch) {
                    return entry.slot;
                }
                if (_filled >= _capacity) {
                    return std::nullopt;
                }
                entry.epoch = _epoch;
                entry.slot  = _filled++;
                return entry.slot;
            }
            index = (index + 1u) & mask;
        }

        // Every slot is occupied (live or stale). Drop stale epochs and retry
        // once so a long frame of distinct textures cannot fail an insert that
        // the 16-slot table still has room for.
        if (!bGrew) {
            rehash();
            return tryAdd(key, true);
        }
        return std::nullopt;
    }

  private:
    void rehash()
    {
        std::vector<Entry> grown(_entries.size() * 2u);
        const uint32_t mask = static_cast<uint32_t>(grown.size() - 1u);
        for (const Entry& entry : _entries) {
            if (entry.epoch != _epoch) {
                continue;
            }
            uint32_t index = static_cast<uint32_t>(hash(entry.key) & mask);
            while (grown[index].epoch != 0) {
                index = (index + 1u) & mask;
            }
            grown[index] = entry;
        }
        _entries = std::move(grown);
    }

    [[nodiscard]] static uint64_t hash(const TextureTableKey& key)
    {
        uint64_t value = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(key.identity));
        value ^= key.imageView + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
        value ^= key.sampler + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
        return value;
    }

    [[nodiscard]] std::optional<uint32_t> insertNew(Entry& entry, const TextureTableKey& key)
    {
        if (_filled >= _capacity) {
            return std::nullopt;
        }
        entry.key   = key;
        entry.epoch = _epoch;
        entry.slot  = _filled++;
        return entry.slot;
    }
};

/// Walk `candidates` in the order given and cut a batch only when the next
/// texture does not fit in the current table. The instance array keeps that
/// order: instance `i` is candidate `i`.
template <typename Range, typename Instance, typename SlotValue, typename KeyFn, typename SlotFn, typename WriteFn>
[[nodiscard]] InstancedDrawPlan<Instance, SlotValue> planInstancedDraws(
    const Range&            candidates,
    const SlotValue&        whiteSlot,
    const TextureTableKey&  whiteKey,
    KeyFn                   keyOf,
    SlotFn                  slotOf,
    WriteFn                 write,
    uint32_t                capacity = TextureTableCursor::kDefaultCapacity)
{
    InstancedDrawPlan<Instance, SlotValue> plan;
    if (candidates.empty()) {
        return plan;
    }

    TextureTableCursor cursor(whiteKey, capacity);
    plan.instances.reserve(candidates.size());

    TextureDrawBatch<SlotValue> batch;
    batch.slots.push_back(whiteSlot);

    auto finish = [&]() {
        if (batch.count == 0) {
            return;
        }
        plan.batches.push_back(std::move(batch));
        batch       = {};
        batch.first = static_cast<uint32_t>(plan.instances.size());
        batch.slots.push_back(whiteSlot);
    };

    for (const auto& candidate : candidates) {
        const TextureTableKey key = keyOf(candidate);
        std::optional<uint32_t> slot = cursor.tryAdd(key);
        if (!slot) {
            finish();
            cursor.beginBatch();
            slot = cursor.tryAdd(key);
            YA_CORE_ASSERT(slot.has_value(), "A fresh texture table cannot hold one sprite");
        }
        const uint32_t placed = slot.value_or(0u);
        if (placed >= batch.slots.size()) {
            batch.slots.resize(placed + 1u);
            batch.slots[placed] = slotOf(candidate);
        }
        Instance instance{};
        write(candidate, placed, instance);
        plan.instances.push_back(std::move(instance));
        ++batch.count;
    }
    finish();
    plan.keyComparisons = cursor.comparisons();
    return plan;
}

inline constexpr uint32_t kScreenDrawUnmappedSlot = ~0u;
/// Catalog index stored for GPU slot 0. The descriptor is the white texture,
/// not a list entry.
inline constexpr uint32_t kScreenDrawWhiteCatalog = ~0u;

/// One already-sealed screen command. Indices address the list's index buffer.
struct ScreenDrawCommandSpan
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    bool     bClipped   = false;
    Rect2D   clip{};
};

/// One GPU batch produced by `planScreenDrawRemap`. Indices in the remap are
/// batch-local; `drawIndexed` adds the batch's vertex base.
struct ScreenDrawBatchRange
{
    uint32_t              firstVertex = 0;
    uint32_t              vertexCount = 0;
    uint32_t              firstIndex  = 0;
    uint32_t              indexCount  = 0;
    bool                  bClipped    = false;
    Rect2D                clip{};
    std::vector<uint32_t> catalogSlots;
};

struct ScreenDrawRemap
{
    std::vector<uint32_t>             sourceVertices;
    std::vector<uint32_t>             gpuSlots;
    std::vector<uint32_t>             indices;
    std::vector<ScreenDrawBatchRange> batches;
};

/// Replay the screen recorder's batch cuts on the CPU.
///
/// Cuts match the pre-migration recorder: a clip change or a vertex/index
/// capacity miss flushes geometry and keeps the texture table; an unmapped
/// catalog slot while the table is full flushes and remaps, including a null
/// texture that would otherwise fit in slot 0. Vertices already emitted in
/// the current batch are reused, so a quad stays four vertices.
template <typename SlotOf>
[[nodiscard]] ScreenDrawRemap planScreenDrawRemap(std::span<const uint32_t>             indices,
                                                  uint32_t                               vertexCount,
                                                  SlotOf                                 slotOf,
                                                  std::span<const TextureTableKey>       catalogKeys,
                                                  const TextureTableKey&                 whiteKey,
                                                  std::span<const ScreenDrawCommandSpan> commands,
                                                  uint32_t                               maxVertices,
                                                  uint32_t                               maxIndices,
                                                  uint32_t tableCapacity = TextureTableCursor::kDefaultCapacity)
{
    ScreenDrawRemap remap;
    TextureTableCursor cursor(whiteKey, tableCapacity);
    cursor.beginBatch();

    std::vector<uint32_t> localToGlobal(catalogKeys.size(), kScreenDrawUnmappedSlot);
    std::vector<uint32_t> liveCatalog{kScreenDrawWhiteCatalog};
    std::unordered_map<uint32_t, uint32_t> copied;
    ScreenDrawBatchRange batch{};
    bool                 bHaveBatch = false;
    bool                 bClipped   = false;
    Rect2D               clip{};

    auto finishBatch = [&]() {
        if (!bHaveBatch) {
            copied.clear();
            return;
        }
        const uint32_t emittedVertices = static_cast<uint32_t>(remap.sourceVertices.size()) - batch.firstVertex;
        const uint32_t emittedIndices  = static_cast<uint32_t>(remap.indices.size()) - batch.firstIndex;
        if (emittedVertices > 0 && emittedIndices > 0) {
            batch.vertexCount  = emittedVertices;
            batch.indexCount   = emittedIndices;
            batch.catalogSlots = liveCatalog;
            remap.batches.push_back(batch);
        }
        bHaveBatch = false;
        copied.clear();
    };
    auto beginBatchRange = [&]() {
        if (bHaveBatch) {
            return;
        }
        batch             = {};
        batch.firstVertex = static_cast<uint32_t>(remap.sourceVertices.size());
        batch.firstIndex  = static_cast<uint32_t>(remap.indices.size());
        batch.bClipped    = bClipped;
        batch.clip        = clip;
        bHaveBatch        = true;
    };
    auto resetTable = [&]() {
        cursor.beginBatch();
        liveCatalog.assign(1, kScreenDrawWhiteCatalog);
        std::fill(localToGlobal.begin(), localToGlobal.end(), kScreenDrawUnmappedSlot);
    };

    auto placeTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
        const uint32_t src[3] = {i0, i1, i2};
        for (int attempt = 0; attempt < 8; ++attempt) {
            uint32_t uniqueNew[3]{};
            uint32_t newVerts         = 0;
            bool     bNeedTextureFlush = false;
            for (uint32_t s : src) {
                if (copied.contains(s)) {
                    continue;
                }
                bool bSeen = false;
                for (uint32_t n = 0; n < newVerts; ++n) {
                    if (uniqueNew[n] == s) {
                        bSeen = true;
                        break;
                    }
                }
                if (bSeen) {
                    continue;
                }
                uniqueNew[newVerts++] = s;
                YA_CORE_ASSERT(s < vertexCount, "Screen draw vertex index out of range");
                const uint32_t catalog = slotOf(s);
                YA_CORE_ASSERT(catalog < localToGlobal.size(), "Screen draw texture slot out of range");
                if (localToGlobal[catalog] == kScreenDrawUnmappedSlot && cursor.filled() >= tableCapacity) {
                    bNeedTextureFlush = true;
                }
            }
            if (bNeedTextureFlush) {
                finishBatch();
                resetTable();
                continue;
            }
            const bool bFits = (remap.sourceVertices.size() - (bHaveBatch ? batch.firstVertex : remap.sourceVertices.size())) + newVerts <= maxVertices
                            && (remap.indices.size() - (bHaveBatch ? batch.firstIndex : remap.indices.size())) + 3 <= maxIndices;
            if (!bFits) {
                YA_CORE_ASSERT(bHaveBatch, "Screen draw triangle does not fit in an empty batch");
                finishBatch();
                continue;
            }
            beginBatchRange();
            auto put = [&](uint32_t s) -> uint32_t {
                if (const auto it = copied.find(s); it != copied.end()) {
                    return it->second;
                }
                const uint32_t catalog = slotOf(s);
                if (localToGlobal[catalog] == kScreenDrawUnmappedSlot) {
                    const std::optional<uint32_t> slot = cursor.tryAdd(catalogKeys[catalog]);
                    YA_CORE_ASSERT(slot.has_value(), "Screen draw texture table overflow without a record-step check");
                    localToGlobal[catalog] = *slot;
                    if (*slot == liveCatalog.size()) {
                        liveCatalog.push_back(catalog);
                    }
                }
                const uint32_t local = static_cast<uint32_t>(remap.sourceVertices.size()) - batch.firstVertex;
                remap.sourceVertices.push_back(s);
                remap.gpuSlots.push_back(localToGlobal[catalog]);
                copied.emplace(s, local);
                return local;
            };
            remap.indices.push_back(put(i0));
            remap.indices.push_back(put(i1));
            remap.indices.push_back(put(i2));
            return;
        }
        YA_CORE_ASSERT(false, "Screen draw failed to place a triangle");
    };

    // `bOpen` is the recorder's command-run flag. A capacity or texture flush
    // closes the geometry batch but leaves the run open, so the next command
    // with the same clip does not flush again.
    bool bOpen = false;
    for (const ScreenDrawCommandSpan& command : commands) {
        const bool bBoundary = !bOpen || bClipped != command.bClipped
                            || (command.bClipped && (clip.pos != command.clip.pos || clip.extent != command.clip.extent));
        if (bBoundary) {
            if (bOpen) {
                finishBatch();
            }
            bOpen    = true;
            bClipped = command.bClipped;
            clip     = command.clip;
        }
        YA_CORE_ASSERT(command.indexCount % 3 == 0, "Screen draw command index count is not a triangle list");
        const uint32_t indexEnd = command.firstIndex + command.indexCount;
        YA_CORE_ASSERT(indexEnd <= indices.size(), "Screen draw command indices out of range");
        for (uint32_t index = command.firstIndex; index < indexEnd; index += 3) {
            placeTriangle(indices[index], indices[index + 1], indices[index + 2]);
        }
    }
    if (bOpen) {
        finishBatch();
    }
    return remap;
}

} // namespace ya
