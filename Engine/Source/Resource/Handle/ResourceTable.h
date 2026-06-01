#pragma once

#include "Resource/Handle/ResourceHandle.h"

#include <memory>
#include <vector>

namespace ya
{

/**
 * @brief Central slot table mapping FResourceHandle<T> -> shared_ptr<T>.
 *
 * This is the indirection layer the "resources are not objects" model relies on:
 * external code holds an 8-byte handle, the table owns the actual object. The
 * underlying object can be replaced (hot-reload), released (recycled) or left
 * empty (lazy/async load pending) without invalidating the handle's identity.
 *
 * Stores shared_ptr<T> so the existing lifetime model (use_count()==1 reclaim +
 * DeferredDeletionQueue GPU-safe destruction) keeps working unchanged.
 *
 * Thread-safety: this table is intentionally NOT internally synchronized. It is
 * a passive data structure; the owner is responsible for serializing access.
 * Like Bevy's `Assets<T>` or UE's FUObjectArray, concurrency is handled one
 * level up (the only production owner, AssetTextureManager, guards every call
 * with its own coarse mutex). Adding a second lock here would only ever be
 * uncontended overhead, so it is left out.
 */
template <typename T>
class ResourceTable
{
    struct Slot
    {
        std::shared_ptr<T> resource;
        uint32_t           generation = 1; // 0 reserved so a default handle never matches
        bool               alive      = false;
    };

    std::vector<Slot>     _slots;
    std::vector<uint32_t> _freeList;

  public:
    /// Allocate a slot. resource may be null (lazy slot, pending async load).
    FResourceHandle<T> allocate(std::shared_ptr<T> res = nullptr)
    {
        uint32_t index;
        if (!_freeList.empty()) {
            index = _freeList.back();
            _freeList.pop_back();
        }
        else {
            index = static_cast<uint32_t>(_slots.size());
            _slots.emplace_back();
        }

        Slot &slot   = _slots[index];
        slot.resource = std::move(res);
        slot.alive    = true;

        return FResourceHandle<T>{.index = index, .generation = slot.generation};
    }

    [[nodiscard]] bool isValid(FResourceHandle<T> h) const
    {
        return h.index < _slots.size() &&
               _slots[h.index].alive &&
               _slots[h.index].generation == h.generation;
    }

    /// Returns the raw object, or nullptr if the handle is stale / slot empty.
    [[nodiscard]] T *get(FResourceHandle<T> h) const
    {
        if (!isValid(h)) {
            return nullptr;
        }
        return _slots[h.index].resource.get();
    }

    [[nodiscard]] std::shared_ptr<T> getShared(FResourceHandle<T> h) const
    {
        if (!isValid(h)) {
            return nullptr;
        }
        return _slots[h.index].resource;
    }

    /**
     * @brief use_count of the slot's shared_ptr, or 0 if stale / empty.
     *
     * Lets owners reclaim unreferenced resources: when the table holds the only
     * reference (count == 1) nobody else is using the object. Reads the slot's
     * shared_ptr in place so it does NOT perturb the count (unlike getShared).
     */
    [[nodiscard]] long useCount(FResourceHandle<T> h) const
    {
        if (!isValid(h) || !_slots[h.index].resource) {
            return 0;
        }
        return _slots[h.index].resource.use_count();
    }

    /// Current generation of the slot, or 0 if the handle is stale.
    [[nodiscard]] uint32_t generationOf(FResourceHandle<T> h) const
    {
        if (h.index >= _slots.size() || !_slots[h.index].alive) {
            return 0;
        }
        return _slots[h.index].generation;
    }

    /**
     * @brief Hot-reload (RCU): swap the object in the same slot and bump generation.
     *
     * Handles keep pointing at the same index; holders that cached the old
     * generation will see a mismatch and re-resolve. The old shared_ptr is
     * returned so the caller can retire it through DeferredDeletionQueue.
     */
    std::shared_ptr<T> replace(FResourceHandle<T> h, std::shared_ptr<T> res)
    {
        if (h.index >= _slots.size() || !_slots[h.index].alive) {
            return nullptr;
        }
        Slot &slot = _slots[h.index];
        std::shared_ptr<T> old = std::move(slot.resource);
        slot.resource = std::move(res);
        ++slot.generation;
        return old;
    }

    /**
     * @brief Recycle a slot: mark dead, bump generation, return to free list.
     *
     * Returns the released shared_ptr so the caller can retire GPU resources via
     * DeferredDeletionQueue. Any existing handle to this slot becomes stale.
     */
    std::shared_ptr<T> release(FResourceHandle<T> h)
    {
        if (h.index >= _slots.size() || !_slots[h.index].alive || _slots[h.index].generation != h.generation) {
            return nullptr;
        }
        Slot &slot = _slots[h.index];
        std::shared_ptr<T> old = std::move(slot.resource);
        slot.alive = false;
        ++slot.generation;
        _freeList.push_back(h.index);
        return old;
    }

    /**
     * @brief Drop every slot at once (scene reset / cache clear).
     *
     * All existing handles become invalid (their index falls outside the table
     * or its generation no longer matches). shared_ptrs are released here, same
     * as the previous map.clear() behaviour.
     */
    void clear()
    {
        _slots.clear();
        _freeList.clear();
    }
};

} // namespace ya
