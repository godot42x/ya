#pragma once

#include "Core/Log.h"
#include "RHI/Core/Texture.h"

#include <cstddef>
#include <cstdint>
#include <optional>
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

} // namespace ya
