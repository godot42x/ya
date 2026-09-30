#pragma once

#include "Core/Base.h"

#include <functional>
#include <mutex>

#include "Core/Common/AssetSlot.h"
#include "Core/Common/Tileset.h"

namespace ya
{

class AssetTilesetManager
{
  private:
    // What one slot update owes its subscribers, gathered under the manager
    // lock and dispatched outside it.
    struct SlotUpdate
    {
        std::vector<std::function<void()>> updateObservers;
    };

    // One shared slot per source path. Tilesets are authoring JSON parsed
    // synchronously on first request, so a slot is only ever Loading between
    // acquire and the parse filling it (same call).
    struct TilesetEntry
    {
        std::shared_ptr<AssetSlot<Tileset>> slot;
        std::string                        filepath; // normalized source path
    };

    std::unordered_map<std::string, TilesetEntry> _entries;
    mutable std::mutex                           _mutex;

  public:
    AssetTilesetManager() = default;

    void clear();

    /// Shared tileset slot for a normalized, non-empty path; parses the
    /// document synchronously on first request. A null handle means no
    /// tileset can be produced (missing/invalid file reads Failed through
    /// the slot).
    AssetHandle<Tileset> acquireTileset(const std::string& path);

    /// Install an engine-generated tileset under a name (mirrors
    /// registerTexture): the entry fills a Ready slot keyed by the name, so
    /// refs bound to the name share it.
    void registerTileset(const std::string& name, const std::shared_ptr<Tileset>& tileset);

    std::shared_ptr<Tileset> getTileset(const std::string& path) const;
    bool                     isTilesetLoaded(const std::string& path) const;

    size_t collectUnused();
    bool   unload(const std::string& path);
    void   invalidate(const std::string& path);

  private:
    // Update the slot and hand the subscribers back for dispatch outside
    // the lock. A null tileset marks the slot Failed. Caller holds _mutex.
    SlotUpdate updateSlotLocked(TilesetEntry& entry, const std::shared_ptr<Tileset>& tileset);
    static void dispatchSlotUpdate(SlotUpdate update);
};

} // namespace ya
