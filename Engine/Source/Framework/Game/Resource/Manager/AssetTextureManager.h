#pragma once

#include <mutex>
#include <unordered_map>

#include "Core/Async/TaskQueue.h"
#include "Resource/AssetManager.h"
#include "Resource/Handle/ResourceTable.h"
#include "Resource/Handle/ResourceTable.h"

namespace ya
{

class AssetTextureManager
{
  private:
    AssetManager& _owner;

    // Slot table owns the Texture objects; the cacheKey map is the index into it.
    // A cacheKey is "path|importSettings", so one path can map to several handles.
    ResourceTable<Texture>                                    _textures;
    std::unordered_map<std::string, FResourceHandle<Texture>> _cacheKey2Handle;
    std::unordered_map<FName, std::string>                    _textureName2Path;
    std::unordered_map<std::string, TaskHandle<AssetManager::TextureMemoryBlock>> _pendingTextureLoads;
    std::unordered_map<std::string, std::string> _failedTextureLoads;
    std::unordered_map<std::string, std::vector<AssetManager::TextureReadyCallback>> _pendingTextureCallbacks;
    std::unordered_map<AssetManager::TextureBatchMemoryHandle, TaskHandle<AssetManager::TextureBatchMemory>>
        _pendingTextureBatchMemoryLoads;
    std::unordered_map<AssetManager::TextureBatchMemoryHandle, AssetManager::TextureBatchMemory>
        _readyTextureBatchMemory;
    AssetManager::TextureBatchMemoryHandle _nextTextureBatchMemoryHandle = 1;
    uint64_t                               _clearGeneration             = 0;
    mutable std::mutex                     _mutex;

  public:
    explicit AssetTextureManager(AssetManager& owner);

    void clear();

    TextureFuture loadTexture(const AssetManager::TextureLoadRequest& request);
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

    bool isTextureLoadPending(const std::string& cacheKey) const;
    bool isTextureLoadFailed(const std::string& filepath) const;

    size_t collectUnused(uint64_t frame);
    bool unload(const std::string& cacheKey, uint64_t frame);
    void invalidate(const std::string& filepath, uint64_t frame);
    void evictCachedAsset(const std::string& assetPath, uint64_t frame);
    void fillStats(AssetManager::CacheStats& stats) const;

  private:
    void submitTextureLoad(const std::string&                           filepath,
                           const std::string&                           cacheKey,
                           AssetManager::ResolvedTextureImportSettings settings,
                           const std::string&                           name = "");
    void registerTextureCallback(const std::string& cacheKey, AssetManager::TextureReadyCallback onReady);
    std::vector<AssetManager::TextureReadyCallback> takeTextureCallbacks(const std::string& cacheKey);
    void dispatchTextureCallbacks(const std::vector<AssetManager::TextureReadyCallback>& callbacks,
                                  const std::shared_ptr<Texture>&                        texture);
    void rememberTextureLoadFailure(const std::string& filepath, std::string reason = {});
    void clearTextureLoadFailure(const std::string& filepath);

    // --- cacheKey <-> slot-table helpers (caller must hold _mutex) ---
    // Look up the cached texture for a cacheKey, or nullptr if absent/stale.
    std::shared_ptr<Texture> findCachedLocked(const std::string& cacheKey) const;
    // Insert or hot-replace the texture for a cacheKey. frame==-1 inserts without
    // retiring; otherwise the replaced object is retired via DeferredDeletionQueue.
    void storeCachedLocked(const std::string& cacheKey, std::shared_ptr<Texture> texture);
    // Drop a cacheKey, retiring its texture through DeferredDeletionQueue.
    void eraseCachedLocked(const std::string& cacheKey, uint64_t frame);
};

} // namespace ya