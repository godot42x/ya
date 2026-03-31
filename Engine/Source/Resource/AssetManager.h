#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

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
        enum class ETextureColorSpace
        {
                SRGB,
                Linear,
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

    // Track in-flight async loads to avoid duplicate submissions
    std::unordered_map<std::string, TaskHandle<DecodedTextureData>> _pendingTextureLoads;
    std::unordered_map<std::string, TaskHandle<DecodedModelData>>   _pendingModelLoads;

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

    AssetMeta getOrLoadMeta(const std::string& assetPath);
    AssetMeta reloadMeta(const std::string& assetPath);

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

    TextureFuture loadTexture(const std::string& filepath,
                              ETextureColorSpace colorSpace = ETextureColorSpace::SRGB);

    TextureFuture loadTexture(const std::string& name,
                              const std::string& filepath,
                              ETextureColorSpace colorSpace = ETextureColorSpace::SRGB);

    TextureFuture loadTexture(const std::string& name,
                              const std::string& filepath,
                              const FName&       textureSemantic);

    /**
     * @brief Load model asynchronously (default).
     *        Returns ModelFuture — check isReady() before using.
     */
    ModelFuture loadModel(const std::string& filepath);
    ModelFuture loadModel(const std::string& name, const std::string& filepath);

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

    /// Core async texture load — submits decode to worker, GPU upload to main-thread callback.
    void submitTextureLoad(const std::string& filepath,
                           const std::string& cacheKey,
                           bool               bSRGB,
                           const std::string& name = "");

    /// Core async model load — submits Assimp decode to worker, GPU mesh creation to main-thread callback.
    void submitModelLoad(const std::string& filepath,
                         const std::string& name = "");
};

} // namespace ya
