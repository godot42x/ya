#pragma once

#include "Core/Common/AssetTypeRegistry.h"
#include "Core/System/VirtualFileSystem.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ya
{

// Synchronous JSON document assets: one shared slot per path, parsed on
// first request, observers and generation updated the same way for every
// document type.
//
// Traits:
//   static std::shared_ptr<T> parse(const std::string& text, std::string& error);
//   static void logCannotRead(const std::string& path);
//   static void logInvalid(const std::string& path, const std::string& error);
//
// logCannotRead / logInvalid are methods so each type keeps a string-literal
// YA_CORE_ERROR format. A null parse result fills the slot Failed.
template <typename T, typename Traits>
class AssetDocumentManager : public IDocumentAssetStore<T>
{
  private:
    // What one slot update owes its subscribers, gathered under the manager
    // lock and dispatched outside it.
    struct SlotUpdate
    {
        std::vector<std::function<void()>> updateObservers;
    };

    // One shared slot per source path. Documents are authoring JSON parsed
    // synchronously on first request, so a slot is only ever Loading between
    // acquire and the parse filling it (same call).
    struct Entry
    {
        std::shared_ptr<AssetSlot<T>> slot;
        std::string                   filepath; // normalized source path
    };

    std::vector<std::string>               _extensions;
    std::unordered_map<std::string, Entry> _entries;
    mutable std::mutex                     _mutex;

  public:
    explicit AssetDocumentManager(std::vector<std::string> extensions)
        : _extensions(std::move(extensions))
    {
    }

    void clear() override
    {
        std::vector<std::function<void()>> updateObservers;
        {
            std::lock_guard lock(_mutex);
            for (auto& [filepath, entry] : _entries) {
                (void)filepath;
                entry.slot->resource.reset();
                entry.slot->state = EAssetSlotState::Failed;
                ++entry.slot->generation;
                auto observers = entry.slot->observers.gather();
                updateObservers.insert(updateObservers.end(),
                                       std::make_move_iterator(observers.begin()),
                                       std::make_move_iterator(observers.end()));
            }
            _entries.clear();
        }
        // Slots went Failed with a generation bump; subscribers hear the update
        // outside the lock.
        for (auto& observer : updateObservers) {
            observer();
        }
    }

    /// Shared slot for a normalized, non-empty path; parses the document
    /// synchronously on first request. A null handle means no document can
    /// be produced (missing/invalid file reads Failed through the slot).
    AssetHandle<T> acquire(const std::string& path) override
    {
        if (path.empty()) {
            return nullptr;
        }

        {
            std::lock_guard lock(_mutex);
            if (auto it = _entries.find(path); it != _entries.end()) {
                return it->second.slot;
            }
        }

        // Parse outside the manager lock: authoring JSON reads go through the
        // VFS and a parse is cheap but not free.
        VirtualFileSystem* vfs = VirtualFileSystem::get();
        std::string        text;
        if (!vfs || !vfs->readFileToString(path, text) || text.empty()) {
            Traits::logCannotRead(path);
        }

        std::shared_ptr<T> resource;
        if (!text.empty()) {
            std::string error;
            resource = Traits::parse(text, error);
            if (!resource) {
                Traits::logInvalid(path, error);
            }
        }

        SlotUpdate     update;
        AssetHandle<T> handle;
        {
            std::lock_guard lock(_mutex);
            auto [it, inserted] = _entries.try_emplace(path);
            Entry& entry        = it->second;
            if (inserted) {
                entry.slot     = std::make_shared<AssetSlot<T>>();
                entry.filepath = path;
            }
            // A racing acquire parses the same document; refilling the shared
            // slot with the same content only bumps the generation.
            update = updateSlotLocked(entry, resource);
            handle = entry.slot;
        }
        dispatchSlotUpdate(std::move(update));
        return handle;
    }

    /// Install an already-built document under a name: the entry fills a
    /// Ready slot keyed by the name, so refs bound to the name share it.
    void registerAsset(const std::string& name, const std::shared_ptr<T>& resource) override
    {
        if (name.empty() || !resource) {
            return;
        }

        SlotUpdate update;
        {
            std::lock_guard lock(_mutex);
            auto [it, inserted] = _entries.try_emplace(name);
            Entry& entry        = it->second;
            if (inserted) {
                entry.slot     = std::make_shared<AssetSlot<T>>();
                entry.filepath = name;
            }
            update = updateSlotLocked(entry, resource);
        }
        dispatchSlotUpdate(std::move(update));
    }

    std::shared_ptr<T> get(const std::string& path) const override
    {
        std::lock_guard lock(_mutex);
        auto            it = _entries.find(path);
        return it != _entries.end() && it->second.slot->state == EAssetSlotState::Ready
                   ? it->second.slot->resource
                   : nullptr;
    }

    bool isLoaded(const std::string& path) const override
    {
        return get(path) != nullptr;
    }

    size_t collectUnused() override
    {
        std::lock_guard lock(_mutex);
        size_t          released = 0;
        for (auto it = _entries.begin(); it != _entries.end();) {
            const auto& slot = it->second.slot;
            // Only the manager holds the slot and nothing outside still uses
            // the document. CPU-only resource: no deferred deletion needed.
            if (slot.use_count() == 1 && (!slot->resource || slot->resource.use_count() == 1)) {
                it = _entries.erase(it);
                ++released;
                continue;
            }
            ++it;
        }
        return released;
    }

    bool ownsPath(const std::string& path) const override
    {
        for (const std::string& extension : _extensions) {
            if (assetPathHasExtension(path, extension)) {
                return true;
            }
        }
        return false;
    }

    bool unload(const std::string& path) override
    {
        SlotUpdate update;
        {
            std::lock_guard lock(_mutex);
            auto            it = _entries.find(path);
            if (it == _entries.end()) {
                return false;
            }
            // Refs still holding the slot see it Failed; the next acquire re-parses.
            update = updateSlotLocked(it->second, nullptr);
            _entries.erase(it);
        }
        dispatchSlotUpdate(std::move(update));
        return true;
    }

    void invalidate(const std::string& path) override
    {
        SlotUpdate update;
        {
            std::lock_guard lock(_mutex);
            auto            it = _entries.find(path);
            if (it == _entries.end()) {
                return;
            }
            // Drop the entry: the next acquire re-parses the (possibly edited)
            // document. Holders of the old slot keep it until they rebind.
            auto observers         = it->second.slot->observers.gather();
            update.updateObservers = std::move(observers);
            _entries.erase(it);
        }
        dispatchSlotUpdate(std::move(update));
    }

  private:
    // Update the slot and hand the subscribers back for dispatch outside
    // the lock. A null resource marks the slot Failed. Caller holds _mutex.
    SlotUpdate updateSlotLocked(Entry& entry, const std::shared_ptr<T>& resource)
    {
        AssetSlot<T>& slot = *entry.slot;
        slot.resource      = resource;
        slot.state         = resource ? EAssetSlotState::Ready : EAssetSlotState::Failed;
        ++slot.generation;
        SlotUpdate update;
        update.updateObservers = slot.observers.gather();
        return update;
    }

    static void dispatchSlotUpdate(SlotUpdate update)
    {
        // Acquire and teardown run on the game thread; observer callbacks only
        // enqueue work.
        for (auto& observer : update.updateObservers) {
            observer();
        }
    }
};

template <typename T, typename Traits, typename RefT>
void AssetTypeRegistry::registerDocument(std::string name, std::string displayName, std::vector<std::string> extensions)
{
    static AssetDocumentManager<T, Traits> manager(extensions);
    AssetTypeDesc                          desc;
    desc.name          = std::move(name);
    desc.displayName   = std::move(displayName);
    desc.extensions    = std::move(extensions);
    desc.refType       = ya::type_index_v<RefT>;
    desc.resourceType  = ya::type_index_v<T>;
    desc.store         = &manager;
    get().registerType(std::move(desc));
}

} // namespace ya
