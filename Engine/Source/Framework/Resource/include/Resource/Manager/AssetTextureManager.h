#pragma once

#include <mutex>
#include <unordered_map>

#include "Core/Async/TaskQueue.h"
#include "Core/Common/AssetSlot.h"
#include "Resource/AssetManager.h"

namespace ya
{

class AssetTextureManager
{
  private:
    // One shared slot per request identity (see requestKey). The entry keeps
    // what a reload needs; the slot is what refs hold.
    struct TextureEntry
    {
        std::shared_ptr<AssetSlot<Texture>> slot;
        std::string                      filepath; // normalized source path; empty for registered textures
        AssetManager::ETextureColorSpace colorSpace = AssetManager::ETextureColorSpace::SRGB;
        // Serial of the decode whose completion may fill the slot; bumped by
        // every (re)load so an older in-flight decode is dropped.
        uint64_t                                        loadSerial = 0;
        std::vector<AssetManager::TextureReadyCallback> readyCallbacks;
    };

    AssetManager& _owner;

    std::unordered_map<std::string, TextureEntry> _entries;
    std::unordered_map<FName, std::string>        _textureName2Key;
    // Handed out when no render backend exists; never cached or filled.
    AssetHandle<Texture> _unavailableSlot;

    std::unordered_map<AssetManager::TextureBatchMemoryHandle, TaskHandle<AssetManager::TextureBatchMemory>>
        _pendingTextureBatchMemoryLoads;
    std::unordered_map<AssetManager::TextureBatchMemoryHandle, AssetManager::TextureBatchMemory>
        _readyTextureBatchMemory;
    AssetManager::TextureBatchMemoryHandle _nextTextureBatchMemoryHandle = 1;
    uint64_t                               _clearGeneration             = 0;
    uint64_t                               _nextLoadSerial              = 0;
    mutable std::mutex                     _mutex;

  public:
    explicit AssetTextureManager(AssetManager& owner);

    void clear();

    AssetHandle<Texture> loadTexture(const AssetManager::TextureLoadRequest& request);
    void loadTextureBatch(const AssetManager::TextureBatchLoadRequest& request);
    AssetManager::TextureBatchMemoryHandle loadTextureBatchIntoMemory(
        const AssetManager::TextureBatchMemoryLoadRequest& request);
    bool consumeTextureBatchMemory(AssetManager::TextureBatchMemoryHandle handle,
                                   AssetManager::TextureBatchMemory&      outBatchMemory);
    std::shared_ptr<Texture> loadTextureSync(const std::string&               name,
                                             const std::string&               filepath,
                                             AssetManager::ETextureColorSpace colorSpace);

    std::shared_ptr<Texture> getTextureByPath(const std::string& filepath) const;
    std::shared_ptr<Texture> getTextureByName(const std::string& name) const;
    bool isTextureLoaded(const std::string& filepath) const;
    bool isTextureLoadedByName(const std::string& name) const;
    void registerTexture(const std::string& name, const stdptr<Texture>& texture);

    bool isTextureLoadFailed(const std::string& filepath) const;

    size_t collectUnused(uint64_t frame);
    bool   unload(const std::string& filepath, uint64_t frame);
    /// Re-decode every variant of a source path into its existing slot.
    void   reload(const std::string& filepath);
    void   fillStats(AssetManager::CacheStats& stats) const;

  private:
    static std::string requestKey(const std::string& normalizedPath, AssetManager::ETextureColorSpace colorSpace);

    // Submit an async decode for an existing entry; the completion fills its slot.
    void submitTextureLoad(const std::string& key, const std::string& filepath, AssetManager::ETextureColorSpace colorSpace);
    void completeTextureLoad(const std::string& key, uint64_t serial, AssetManager::TextureMemoryBlock decoded);
    std::shared_ptr<Texture> uploadTexture(const AssetManager::TextureMemoryBlock& decoded, const std::string& label);

    // Fill the slot and hand the callbacks back for dispatch outside the lock.
    // A null texture marks the slot Failed. Caller holds _mutex.
    std::vector<AssetManager::TextureReadyCallback> fillSlotLocked(TextureEntry& entry, const std::shared_ptr<Texture>& texture);
    static void dispatchTextureCallbacks(std::vector<AssetManager::TextureReadyCallback> callbacks,
                                         const std::shared_ptr<Texture>&                 texture);
};

} // namespace ya
