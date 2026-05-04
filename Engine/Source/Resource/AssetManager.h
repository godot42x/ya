#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Core/Common/AssetFuture.h"
#include "Resource/AssetManagerTypes.h"
#include "Resource/Meta/AssetMeta.h"
#include "Resource/ResourceRegistry.h"


namespace Assimp
{
struct Importer;
}

namespace ya
{

class AssetModelManager;
class AssetTextureManager;

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
    using TextureBatchMemoryHandle      = AssetTextureBatchMemoryHandle;
    using ETextureColorSpace            = AssetTextureColorSpace;
    using ETextureSourceKind            = AssetTextureSourceKind;
    using ETexturePayloadType           = AssetTexturePayloadType;
    using ETextureDecodePrecision       = AssetTextureDecodePrecision;
    using ETextureChannelPolicy         = AssetTextureChannelPolicy;
    using ETextureUploadStrategy        = AssetTextureUploadStrategy;
    using ETextureTranscodeTarget       = AssetTextureTranscodeTarget;
    using TextureFormatTraits           = AssetTextureFormatTraits;
    using TextureSourceInfo             = AssetTextureSourceInfo;
    using ResolvedTextureImportSettings = AssetResolvedTextureImportSettings;
    using TextureMemoryBlock            = AssetTextureMemoryBlock;
    using TextureBatchMemory            = AssetTextureBatchMemory;
    using TextureReadyCallback          = AssetTextureReadyCallback;
    using ModelReadyCallback            = AssetModelReadyCallback;
    using TextureBatchReadyCallback     = AssetTextureBatchReadyCallback;
    using TextureLoadRequest            = AssetTextureLoadRequest;
    using TextureBatchLoadRequest       = AssetTextureBatchLoadRequest;
    using TextureBatchMemoryLoadRequest = AssetTextureBatchMemoryLoadRequest;
    using ModelLoadRequest              = AssetModelLoadRequest;

    /// Returns the traits for a supported texture upload format, or nullptr if unknown.
    static const TextureFormatTraits* getFormatTraits(EFormat::T format);
    static std::string normalizeAssetPath(std::string path);

  private:
    friend class AssetTextureManager;
    friend class AssetModelManager;

    //  use file as a renderTargets
    std::unordered_map<FName, stdptr<Texture>> _renderTexture;

    // Meta cache: assetPath -> loaded AssetMeta
    std::unordered_map<std::string, AssetMeta> _metaCache;

    // Resource version: incremented on reload/invalidate. TAssetRef compares against this.
    std::unordered_map<std::string, uint64_t> _resourceVersion;

    std::unique_ptr<AssetTextureManager> _textureManager;
    std::unique_ptr<AssetModelManager>   _modelManager;

  public:
    static AssetManager* get();

    // IResourceCache interface
    void  clearCache() override;
    FName getCacheName() const override { return "AssetManager"; }

    AssetManager();
    ~AssetManager();

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

    static ETextureColorSpace     inferTextureColorSpace(const FName& textureSemantic);
    TextureSourceInfo             inspectTextureSource(const std::string& filepath) const;
    ResolvedTextureImportSettings resolveTextureImportSettings(const std::string& filepath,
                                                               ETextureColorSpace codeHint);

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

    static std::optional<EFormat::T> parseTextureFormatOverride(const std::string& formatName);
    static const char*               textureSourceKindName(ETextureSourceKind kind);
    static const char*               texturePayloadTypeName(ETexturePayloadType type);
    static const char*               textureColorSpaceName(ETextureColorSpace colorSpace);

    /// Check whether an async texture load is still in flight.
    bool isTextureLoadPending(const std::string& cacheKey) const;

    /// Check whether a texture path has a remembered hard failure.
    bool isTextureLoadFailed(const std::string& filepath) const;

    /// Check whether an async model load is still in flight.
    bool isModelLoadPending(const std::string& filepath) const;

  private:
    /// Get or compute+cache the cacheKey for a filepath. Returns a stable reference.
    std::string buildTextureCacheKey(const std::string&                   requestPath,
                                     const ResolvedTextureImportSettings& settings);

    static void dispatchToGameThread(std::function<void()> task);

    AssetTextureManager&       textureManager();
    const AssetTextureManager& textureManager() const;
    AssetModelManager&         modelManager();
    const AssetModelManager&   modelManager() const;

    void evictCachedAsset(const std::string& assetPath);
};

} // namespace ya
