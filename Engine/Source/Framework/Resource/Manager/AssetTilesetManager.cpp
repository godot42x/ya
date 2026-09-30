#include "Resource/Manager/AssetTilesetManager.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

namespace ya
{

void AssetTilesetManager::clear()
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

AssetHandle<Tileset> AssetTilesetManager::acquireTileset(const std::string& path){
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
        YA_CORE_ERROR("AssetTilesetManager: cannot read tileset '{}'", path);
    }

    std::shared_ptr<Tileset> tileset;
    if (!text.empty()) {
        std::string error;
        tileset = parseTilesetJson(text, error);
        if (!tileset) {
            YA_CORE_ERROR("AssetTilesetManager: '{}' is not a valid tileset: {}", path, error);
        }
    }

    SlotUpdate update;
    AssetHandle<Tileset> handle;
    {
        std::lock_guard lock(_mutex);
        auto [it, inserted] = _entries.try_emplace(path);
        TilesetEntry& entry = it->second;
        if (inserted) {
            entry.slot     = std::make_shared<AssetSlot<Tileset>>();
            entry.filepath = path;
        }
        // A racing acquire parses the same document; refilling the shared
        // slot with the same content only bumps the generation.
        update = updateSlotLocked(entry, tileset);
        handle = entry.slot;
    }
    dispatchSlotUpdate(std::move(update));
    return handle;
}

void AssetTilesetManager::registerTileset(const std::string& name, const std::shared_ptr<Tileset>& tileset)
{
    if (name.empty() || !tileset) {
        return;
    }

    SlotUpdate update;
    {
        std::lock_guard lock(_mutex);
        auto [it, inserted] = _entries.try_emplace(name);
        TilesetEntry& entry = it->second;
        if (inserted) {
            entry.slot     = std::make_shared<AssetSlot<Tileset>>();
            entry.filepath = name;
        }
        update = updateSlotLocked(entry, tileset);
    }
    dispatchSlotUpdate(std::move(update));
}

std::shared_ptr<Tileset> AssetTilesetManager::getTileset(const std::string& path) const
{
    std::lock_guard lock(_mutex);
    auto            it = _entries.find(path);
    return it != _entries.end() && it->second.slot->state == EAssetSlotState::Ready
               ? it->second.slot->resource
               : nullptr;
}

bool AssetTilesetManager::isTilesetLoaded(const std::string& path) const
{
    return getTileset(path) != nullptr;
}

size_t AssetTilesetManager::collectUnused()
{
    std::lock_guard lock(_mutex);
    size_t          released = 0;
    for (auto it = _entries.begin(); it != _entries.end();) {
        const auto& slot = it->second.slot;
        // Only the manager holds the slot and nothing outside still uses the
        // tileset. CPU-only resource: no deferred deletion needed.
        if (slot.use_count() == 1 && (!slot->resource || slot->resource.use_count() == 1)) {
            it = _entries.erase(it);
            ++released;
            continue;
        }
        ++it;
    }
    return released;
}

bool AssetTilesetManager::unload(const std::string& path)
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

void AssetTilesetManager::invalidate(const std::string& path)
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
        auto observers = it->second.slot->observers.gather();
        update.updateObservers = std::move(observers);
        _entries.erase(it);
    }
    dispatchSlotUpdate(std::move(update));
}

AssetTilesetManager::SlotUpdate AssetTilesetManager::updateSlotLocked(TilesetEntry&                   entry,
                                                                     const std::shared_ptr<Tileset>& tileset)
{
    AssetSlot<Tileset>& slot = *entry.slot;
    slot.resource = tileset;
    slot.state    = tileset ? EAssetSlotState::Ready : EAssetSlotState::Failed;
    ++slot.generation;
    SlotUpdate update;
    update.updateObservers = slot.observers.gather();
    return update;
}

void AssetTilesetManager::dispatchSlotUpdate(SlotUpdate update)
{
    // Acquire and teardown run on the game thread; observer callbacks only
    // enqueue work.
    for (auto& observer : update.updateObservers) {
        observer();
    }
}

} // namespace ya
