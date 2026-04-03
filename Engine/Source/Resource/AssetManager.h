#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Async/TaskQueue.h"
#include "Core/Common/AssetFuture.h"
#include "Render/Core/Texture.h"
#include "Render/Model.h"
#include "Resource/AssetMeta.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Resource/ResourceRegistry.h"


namespace Assimp
{
struct Importer;
}

namespace ya
{

namespace EResource
{
enum T
{
    None = 0,
    Texture,
    Model,

};


};

struct Resource
{
};


class AssetManager : public IResourceCache
{
  public:
    using TextureBatchMemoryHandle = uint64_t;

    struct TextureMemoryBlock
    {
        std::string          filepath;
        uint32_t             width    = 0;
        uint32_t             height   = 0;
        uint32_t             channels = 4;
        EFormat::T           format   = EFormat::R8G8B8A8_UNORM;
        std::vector<uint8_t> bytes;

        [[nodiscard]] bool isValid() const
        {
            return !bytes.empty() && width > 0 && height > 0;
        }

        [[nodiscard]] size_t dataSize() const
        {
            return bytes.size();
        }
    };

    struct TextureBatchMemory
    {
        std::vector<TextureMemoryBlock> textures;

        [[nodiscard]] bool isValid() const
        {
            if (textures.empty()) {
                return false;
            }

            for (const auto& texture : textures) {
                if (!texture.isValid()) {
                    return false;
                }
            }

            return true;
        }
    };

    using TextureReadyCallback      = std::function<void(const std::shared_ptr<Texture>&)>;
    using ModelReadyCallback        = std::function<void(const std::shared_ptr<Model>&)>;
    using TextureBatchReadyCallback = std::function<void(const std::vector<std::shared_ptr<Texture>>&)>;

    enum class ETextureColorSpace
    {
        SRGB,
        Linear,
    };

    struct TextureLoadRequest
    {
        std::string          filepath;
        std::string          name;
        TextureReadyCallback onReady;
        ETextureColorSpace   colorSpace = ETextureColorSpace::SRGB;
        std::optional<FName> textureSemantic;
    };

    struct TextureBatchLoadRequest
    {
        std::vector<std::string>  filepaths;
        TextureBatchReadyCallback onDone;
        ETextureColorSpace        colorSpace = ETextureColorSpace::SRGB;
    };

    struct TextureBatchMemoryLoadRequest
    {
        std::vector<std::string> filepaths;
        ETextureColorSpace       colorSpace = ETextureColorSpace::SRGB;
    };

    struct ModelLoadRequest
    {
        std::string        filepath;
        std::string        name;
        ModelReadyCallback onReady;
    };

  private:

    // Cache for loaded models  (key = cacheKey = path | propertiesHash)
    std::unordered_map<std::string, std::shared_ptr<Model>> modelCache;
    std::unordered_map<std::string, std::string>            _modalName2Path;

    // Cache for loaded textures (key = cacheKey = path | propertiesHash)
    std::unordered_map<std::string, std::shared_ptr<Texture>> _textureViews;
    std::unordered_map<FName, std::string>                    _textureName2Path;

    //  use file as a renderTargets
    std::unordered_map<FName, stdptr<Texture>> _renderTexture;

    // Meta cache: assetPath -> loaded AssetMeta
    std::unordered_map<std::string, AssetMeta> _metaCache;

    // Cached path → cacheKey mapping. Avoids re-computing propertiesHash() every frame
    // when TAssetRef::resolve() calls loadTexture() repeatedly for in-flight assets.
    // Invalidated on meta change (onMetaFileChanged / reloadMeta).
    std::unordered_map<std::string, std::string> _cacheKeyCache;

    // Resource version: incremented on reload/invalidate. TAssetRef compares against this.
    std::unordered_map<std::string, uint64_t> _resourceVersion;

    // Track in-flight async loads to avoid duplicate submissions
    std::unordered_map<std::string, TaskHandle<TextureMemoryBlock>>              _pendingTextureLoads;
    std::unordered_map<std::string, TaskHandle<DecodedModelData>>                _pendingModelLoads;
    std::unordered_map<std::string, std::vector<TextureReadyCallback>>           _pendingTextureCallbacks;
    std::unordered_map<std::string, std::vector<ModelReadyCallback>>             _pendingModelCallbacks;
    std::unordered_map<TextureBatchMemoryHandle, TaskHandle<TextureBatchMemory>> _pendingTextureBatchMemoryLoads;
    std::unordered_map<TextureBatchMemoryHandle, TextureBatchMemory>             _readyTextureBatchMemory;
    TextureBatchMemoryHandle                                                     _nextTextureBatchMemoryHandle = 1;

    // Mutex for thread-safe cache access during async loading
    mutable std::mutex _cacheMutex;

  public:
    static AssetManager* get();

    // IResourceCache interface
    void  clearCache() override;
    FName getCacheName() const override { return "AssetManager"; }

    AssetManager();
    ~AssetManager()
    {
        YA_CORE_INFO("AssetManager destructor");
    }

    // ── Meta system ─────────────────────────────────────────────────────

    const AssetMeta& getOrLoadMeta(const std::string& assetPath);
    const AssetMeta& reloadMeta(const std::string& assetPath);

    // ── Cache key helpers ───────────────────────────────────────────────

    static std::string makeCacheKey(const std::string& filepath, const AssetMeta& meta);

    // ── Default loading (async) ─────────────────────────────────────────
    //
    // loadTexture() / loadModel() are ASYNC by default.
    // They return AssetFuture<T> — a type-safe wrapper that FORCES you to
    // check isReady() before accessing the resource.
    //
    // Use loadTextureSync() / loadModelSync() for must-have-now resources.
    // Sync methods return shared_ptr<T> directly (guaranteed non-null on success).

    TextureFuture loadTexture(const TextureLoadRequest& request);

    ModelFuture loadModel(const ModelLoadRequest& request);

    void                     loadTextureBatch(const TextureBatchLoadRequest& request);
    TextureBatchMemoryHandle loadTextureBatchIntoMemory(const TextureBatchMemoryLoadRequest& request);
    bool                     consumeTextureBatchMemory(TextureBatchMemoryHandle handle, TextureBatchMemory& outBatchMemory);

    /**
     * @brief Load model asynchronously (default).
     *        Returns ModelFuture — check isReady() before using.
     */
    // ── Explicit synchronous loading ────────────────────────────────────

    std::shared_ptr<Texture> loadTextureSync(const std::string& filepath,
                                             ETextureColorSpace colorSpace = ETextureColorSpace::SRGB);

    std::shared_ptr<Texture> loadTextureSync(const std::string& name,
                                             const std::string& filepath,
                                             ETextureColorSpace colorSpace = ETextureColorSpace::SRGB);

    /**
     * @brief Load model synchronously — blocks until Assimp decode + GPU mesh creation complete.
     */
    std::shared_ptr<Model> loadModelSync(const std::string& filepath);
    std::shared_ptr<Model> loadModelSync(const std::string& name, const std::string& filepath);

    std::shared_ptr<Model> getModel(const std::string& filepath) const;
    bool                   isModelLoaded(const std::string& filepath) const;

    static ETextureColorSpace inferTextureColorSpace(const FName& textureSemantic);

    // ── Reference counting / auto-release ───────────────────────────────

    /**
     * @brief Remove cached resources whose only reference is the cache itself
     *        (shared_ptr use_count == 1).
     *        GPU-SAFE: deferred via DeferredDeletionQueue.
     */
    size_t collectUnused();

    /**
     * @brief Force-unload a specific resource by its cache key.
     *        GPU-SAFE: deferred via DeferredDeletionQueue.
     */
    void unload(const std::string& cacheKey);

    struct CacheStats
    {
        size_t textureCount          = 0;
        size_t modelCount            = 0;
        size_t textureMemoryEstimate = 0;
    };
    CacheStats getStats() const;

    // ── Hot-reload ──────────────────────────────────────────────────────

    void onMetaFileChanged(const std::string& metaPath);
    void onAssetFileChanged(const std::string& assetPath);

    // ── Resource versioning (for TAssetRef stale-pointer detection) ─────

    /**
     * @brief Get current version for a resource path. Incremented on every
     *        reload / invalidate / meta change. TAssetRef compares this against
     *        its cached version to detect stale pointers.
     */
    uint64_t getResourceVersion(const std::string& assetPath) const;

    /**
     * @brief Force-bump the version for a resource path.
     *        Called automatically by invalidate/onMetaFileChanged/onAssetFileChanged.
     */
    void bumpResourceVersion(const std::string& assetPath);

    // ── Query ───────────────────────────────────────────────────────────

    std::shared_ptr<Texture> getTextureByPath(const std::string& filepath) const;
    std::shared_ptr<Texture> getTextureByName(const std::string& name) const;

    bool isTextureLoaded(const std::string& filepath) const;
    bool isTextureLoadedByName(const std::string& name) const;

    void registerTexture(const std::string& name, const stdptr<Texture>& texture);

    void invalidate(const std::string& filepath) override;

    /// Check whether an async texture load is still in flight.
    bool isTextureLoadPending(const std::string& cacheKey) const;

    /// Check whether an async model load is still in flight.
    bool isModelLoadPending(const std::string& filepath) const;

  private:
    std::shared_ptr<Model> loadModelImpl(const std::string& filepath, const std::string& identifier);

    ETextureColorSpace resolveColorSpace(const std::string& filepath,
                                         ETextureColorSpace codeHint);

    std::vector<std::string> findCacheKeysForPath(const std::string& filepath) const;

    /// Get or compute+cache the cacheKey for a filepath. Returns a stable reference.
    const std::string& getOrBuildCacheKey(const std::string& filepath);

    /// Core async texture load — submits decode to worker, GPU upload to main-thread callback.
    void submitTextureLoad(const std::string& filepath,
                           const std::string& cacheKey,
                           bool               bSRGB,
                           const std::string& name = "");

    /// Core async model load — submits Assimp decode to worker, GPU mesh creation to main-thread callback.
    void submitModelLoad(const std::string& filepath,
                         const std::string& name = "");

    static void dispatchToGameThread(std::function<void()> task);

    void                              registerTextureCallback(const std::string& cacheKey, TextureReadyCallback onReady);
    void                              registerModelCallback(const std::string& filepath, ModelReadyCallback onReady);
    std::vector<TextureReadyCallback> takeTextureCallbacks(const std::string& cacheKey);
    std::vector<ModelReadyCallback>   takeModelCallbacks(const std::string& filepath);
    void                              dispatchTextureCallbacks(const std::vector<TextureReadyCallback>& callbacks,
                                                               const std::shared_ptr<Texture>&          texture);
    void                              dispatchModelCallbacks(const std::vector<ModelReadyCallback>& callbacks,
                                                             const std::shared_ptr<Model>&          model);
};

} // namespace ya
