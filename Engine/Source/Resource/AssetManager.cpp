#include "AssetManager.h"

#include "Core/Debug/Instrumentor.h"
#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"
#include "Resource/AssetMeta.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Resource/TextureLibrary.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <memory>

namespace ya
{


// ============================================================================
// Singleton
// ============================================================================

AssetManager* AssetManager::get()
{
    static AssetManager instance;
    return &instance;
}

AssetManager::AssetManager()
{
}

// ============================================================================
// IResourceCache
// ============================================================================

void AssetManager::clearCache()
{
    YA_PROFILE_FUNCTION_LOG();
    std::lock_guard lock(_cacheMutex);
    modelCache.clear();
    _textureViews.clear();
    _metaCache.clear();
    _cacheKeyCache.clear();

    YA_CORE_INFO("AssetManager cleared");
}

// ============================================================================
// Meta system
// ============================================================================

const AssetMeta& AssetManager::getOrLoadMeta(const std::string& assetPath)
{
    // Check meta cache first
    {
        auto it = _metaCache.find(assetPath);
        if (it != _metaCache.end()) {
            return it->second;
        }
    }

    // Try to load from sidecar file
    std::string metaPath = AssetMeta::metaPathFor(assetPath);

    // Try physical path first, then VFS-translated path
    AssetMeta meta;
    if (std::filesystem::exists(metaPath)) {
        meta = AssetMeta::loadFromFile(metaPath);
    }
    else {
        // Also try VFS-translated path
        auto* vfs           = VirtualFileSystem::get();
        auto  translatedPath = vfs->translatePath(metaPath).string();
        if (std::filesystem::exists(translatedPath)) {
            meta = AssetMeta::loadFromFile(translatedPath);
        }
        else {
            // No meta file — infer defaults from extension
            std::string ext = assetPath.substr(assetPath.find_last_of('.') + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" ||
                ext == "tga" || ext == "hdr" || ext == "dds") {
                meta = AssetMeta::defaultForTexture();
            }
            else if (ext == "obj" || ext == "fbx" || ext == "gltf" || ext == "glb" ||
                     ext == "dae" || ext == "blend") {
                meta = AssetMeta::defaultForModel();
            }
            else {
                // Generic fallback
                meta.type = "unknown";
            }

            YA_CORE_TRACE("AssetMeta: no sidecar for '{}', using default (type={})", assetPath, meta.type);
        }
    }

    // Cache
    auto [it2, _] = _metaCache.emplace(assetPath, std::move(meta));
    return it2->second;
}

const AssetMeta& AssetManager::reloadMeta(const std::string& assetPath)
{
    _metaCache.erase(assetPath);
    _cacheKeyCache.erase(assetPath);  // Force re-compute on next loadTexture
    return getOrLoadMeta(assetPath);
}

// ============================================================================
// Cache key
// ============================================================================

std::string AssetManager::makeCacheKey(const std::string& filepath, const AssetMeta& meta)
{
    return filepath + "|" + std::to_string(meta.propertiesHash());
}

const std::string& AssetManager::getOrBuildCacheKey(const std::string& filepath)
{
    const auto it = _cacheKeyCache.find(filepath);
    if (it != _cacheKeyCache.end())
        return it->second;

    const auto& meta = getOrLoadMeta(filepath);
    auto [inserted, _] = _cacheKeyCache.emplace(filepath, makeCacheKey(filepath, meta));
    return inserted->second;
}

// ============================================================================
// Synchronous texture loading (meta-driven)
// ============================================================================

AssetManager::ETextureColorSpace AssetManager::resolveColorSpace(const std::string& filepath,
                                                                  ETextureColorSpace codeHint)
{
    const AssetMeta&   meta  = getOrLoadMeta(filepath);
    const std::string  csStr = meta.getString("colorSpace", "");

    if (csStr.empty()) {
        return codeHint;
    }

    const ETextureColorSpace metaCS = (csStr == "linear") ? ETextureColorSpace::Linear : ETextureColorSpace::SRGB;

    // Only log once per unique filepath to avoid per-frame spam.
    // The override is expected and normal — not worth logging every call.

    return metaCS;
}

AssetManager::ETextureColorSpace AssetManager::inferTextureColorSpace(const FName& textureSemantic)
{
    if (textureSemantic == MatTexture::Diffuse ||
        textureSemantic == MatTexture::Albedo ||
        textureSemantic == MatTexture::Specular ||
        textureSemantic == MatTexture::Emissive)
    {
        return ETextureColorSpace::SRGB;
    }

    if (textureSemantic == MatTexture::Normal ||
        textureSemantic == MatTexture::Metallic ||
        textureSemantic == MatTexture::Roughness ||
        textureSemantic == MatTexture::AO)
    {
        return ETextureColorSpace::Linear;
    }

    return ETextureColorSpace::SRGB;
}

TextureFuture AssetManager::loadTexture(const std::string& filepath, ETextureColorSpace colorSpace)
{
    const auto  resolvedCS = resolveColorSpace(filepath, colorSpace);
    const bool  bSRGB      = (resolvedCS == ETextureColorSpace::SRGB);
    const auto& cacheKey   = getOrBuildCacheKey(filepath);

    // Already loaded? Return immediately.
    {
        const auto it = _textureViews.find(cacheKey);
        if (it != _textureViews.end()) {
            return TextureFuture(it->second);
        }
    }

    // Submit async decode if not already in flight
    if (!isTextureLoadPending(cacheKey)) {
        submitTextureLoad(filepath, cacheKey, bSRGB);
    }

    // Not ready yet — return empty future. Caller must check isReady().
    return TextureFuture();
}

TextureFuture AssetManager::loadTexture(const std::string& name,
                                        const std::string& filepath,
                                        ETextureColorSpace colorSpace)
{
    const auto  resolvedCS = resolveColorSpace(filepath, colorSpace);
    const bool  bSRGB      = (resolvedCS == ETextureColorSpace::SRGB);
    const auto& cacheKey   = getOrBuildCacheKey(filepath);

    // Already loaded?
    {
        const auto it = _textureViews.find(cacheKey);
        if (it != _textureViews.end()) {
            return TextureFuture(it->second);
        }
    }

    if (!isTextureLoadPending(cacheKey)) {
        submitTextureLoad(filepath, cacheKey, bSRGB, name);
    }

    _textureName2Path[name] = cacheKey;
    return TextureFuture();
}

TextureFuture AssetManager::loadTexture(const std::string& name,
                                        const std::string& filepath,
                                        const FName&       textureSemantic)
{
    return loadTexture(name, filepath, inferTextureColorSpace(textureSemantic));
}

// ============================================================================
// Explicit synchronous loading
// ============================================================================

std::shared_ptr<Texture> AssetManager::loadTextureSync(const std::string& filepath,
                                                        ETextureColorSpace colorSpace)
{
    const auto         resolvedCS = resolveColorSpace(filepath, colorSpace);
    const auto&        cacheKey   = getOrBuildCacheKey(filepath);
    const bool         bSRGB      = (resolvedCS == ETextureColorSpace::SRGB);

    // Check cache
    {
        const auto it = _textureViews.find(cacheKey);
        if (it != _textureViews.end()) {
            return it->second;
        }
    }

    // Legacy path-only key backward compat
    {
        auto it = _textureViews.find(filepath);
        if (it != _textureViews.end()) {
            auto tex            = it->second;
            auto expectedFormat = bSRGB ? EFormat::R8G8B8A8_SRGB : EFormat::R8G8B8A8_UNORM;
            if (tex && tex->getFormat() != expectedFormat) {
                YA_CORE_WARN("Texture '{}' already loaded as format {}, requested {}. Loading a new copy.",
                             filepath, (int)tex->getFormat(), (int)expectedFormat);
            }
            else {
                _textureViews[cacheKey] = tex;
                return tex;
            }
        }
    }

    // Synchronous: decode + GPU upload on calling thread
    auto texture = Texture::fromFile(filepath, "", bSRGB);
    if (!texture) {
        YA_CORE_WARN("loadTextureSync: Failed to create texture: {}", filepath);
        return nullptr;
    }

    _textureViews[cacheKey] = texture;
    return texture;
}

std::shared_ptr<Texture> AssetManager::loadTextureSync(const std::string& name,
                                                        const std::string& filepath,
                                                        ETextureColorSpace colorSpace)
{
    const auto  resolvedCS = resolveColorSpace(filepath, colorSpace);
    const auto& cacheKey   = getOrBuildCacheKey(filepath);
    const bool  bSRGB      = (resolvedCS == ETextureColorSpace::SRGB);

    {
        const auto it = _textureViews.find(cacheKey);
        if (it != _textureViews.end()) {
            return it->second;
        }
    }

    auto texture = Texture::fromFile(filepath, name, bSRGB);
    if (!texture) {
        YA_CORE_WARN("loadTextureSync: Failed to create texture: {}", filepath);
    }

    _textureViews[cacheKey] = texture;
    _textureName2Path[name] = cacheKey;
    return texture;
}

// ============================================================================
// Core async texture pipeline
// ============================================================================

void AssetManager::submitTextureLoad(const std::string& filepath,
                                     const std::string& cacheKey,
                                     bool               bSRGB,
                                     const std::string& name)
{
    YA_CORE_INFO("submitTextureLoad: async decode '{}' (sRGB={})", filepath, bSRGB);

    auto handle = TaskQueue::get().submitWithCallback(
        // ── Worker thread: CPU-only decode (stbi_load) ──────────────
        [filepath, bSRGB]() -> DecodedTextureData {
            return DecodedTextureData::decode(filepath, "", bSRGB);
        },
        // ── Main thread callback: GPU upload ────────────────────────
        [this, filepath, cacheKey, name](DecodedTextureData decoded) {
            // Another load may have raced and filled the cache already
            {
                std::lock_guard lock(_cacheMutex);
                if (_textureViews.find(cacheKey) != _textureViews.end()) {
                    _pendingTextureLoads.erase(cacheKey);
                    return;
                }
            }

            if (!decoded.isValid()) {
                YA_CORE_WARN("Async texture decode failed for '{}'", filepath);
                std::lock_guard lock(_cacheMutex);
                _pendingTextureLoads.erase(cacheKey);
                return;
            }

            // GPU upload on main thread
            auto texture = Texture::fromDecodedData(std::move(decoded));
            if (!texture) {
                YA_CORE_WARN("Async GPU upload failed for '{}'", filepath);
                std::lock_guard lock(_cacheMutex);
                _pendingTextureLoads.erase(cacheKey);
                return;
            }

            // Store in cache
            {
                std::lock_guard lock(_cacheMutex);
                _textureViews[cacheKey] = texture;
                if (!name.empty()) {
                    _textureName2Path[name] = cacheKey;
                }
                _pendingTextureLoads.erase(cacheKey);
            }

            YA_CORE_INFO("Async texture ready: '{}' ({}x{})", filepath, texture->getWidth(), texture->getHeight());
        });

    std::lock_guard lock(_cacheMutex);
    _pendingTextureLoads[cacheKey] = std::move(handle);
}

// ============================================================================
// Resource versioning
// ============================================================================

uint64_t AssetManager::getResourceVersion(const std::string& assetPath) const
{
    auto it = _resourceVersion.find(assetPath);
    return (it != _resourceVersion.end()) ? it->second : 0;
}

void AssetManager::bumpResourceVersion(const std::string& assetPath)
{
    const auto newVersion = ++_resourceVersion[assetPath];
    YA_CORE_TRACE("bumpResourceVersion: '{}' → v{}", assetPath, newVersion);
}

bool AssetManager::isTextureLoadPending(const std::string& cacheKey) const
{
    std::lock_guard lock(_cacheMutex);
    auto it = _pendingTextureLoads.find(cacheKey);
    if (it == _pendingTextureLoads.end()) return false;
    return !it->second.isReady();
}

// ============================================================================
// Reference counting / auto-release (GPU-safe via DeferredDeletionQueue)
// ============================================================================

/// Helper: get the current frame index tracked by the deferred deletion queue.
static uint64_t getCurrentFrameIdx()
{
    return DeferredDeletionQueue::get().currentFrame();
}

size_t AssetManager::collectUnused()
{
    size_t released = 0;
    auto&  ddq      = DeferredDeletionQueue::get();
    uint64_t frame  = getCurrentFrameIdx();

    // Textures
    for (auto it = _textureViews.begin(); it != _textureViews.end();) {
        if (it->second && it->second.use_count() == 1) {
            YA_CORE_TRACE("collectUnused: scheduling deferred release for texture '{}'", it->first);
            // Move the shared_ptr into the deferred queue — actual destructor
            // (VulkanImage::~VulkanImage → vkDestroyImage) runs only after
            // the GPU has finished all in-flight frames.
            ddq.enqueueResource(frame, std::move(it->second));
            it = _textureViews.erase(it);
            ++released;
        }
        else {
            ++it;
        }
    }

    // Models (contain GPU Mesh buffers)
    for (auto it = modelCache.begin(); it != modelCache.end();) {
        if (it->second && it->second.use_count() == 1) {
            YA_CORE_TRACE("collectUnused: scheduling deferred release for model '{}'", it->first);
            ddq.enqueueResource(frame, std::move(it->second));
            it = modelCache.erase(it);
            ++released;
        }
        else {
            ++it;
        }
    }

    if (released > 0) {
        YA_CORE_INFO("collectUnused: {} resources scheduled for deferred release", released);
    }

    return released;
}

void AssetManager::unload(const std::string& cacheKey)
{
    auto&    ddq   = DeferredDeletionQueue::get();
    uint64_t frame = getCurrentFrameIdx();

    auto texIt = _textureViews.find(cacheKey);
    if (texIt != _textureViews.end()) {
        ddq.enqueueResource(frame, std::move(texIt->second));
        _textureViews.erase(texIt);
        YA_CORE_INFO("Force-unloaded texture (deferred): {}", cacheKey);
        return;
    }

    auto modelIt = modelCache.find(cacheKey);
    if (modelIt != modelCache.end()) {
        ddq.enqueueResource(frame, std::move(modelIt->second));
        modelCache.erase(modelIt);
        YA_CORE_INFO("Force-unloaded model (deferred): {}", cacheKey);
        return;
    }

    YA_CORE_WARN("unload: cache key '{}' not found", cacheKey);
}

AssetManager::CacheStats AssetManager::getStats() const
{
    CacheStats stats;
    stats.textureCount = _textureViews.size();
    stats.modelCount   = modelCache.size();

    for (auto& [key, tex] : _textureViews) {
        if (tex) {
            stats.textureMemoryEstimate += static_cast<size_t>(tex->getWidth()) *
                                            tex->getHeight() * tex->getChannels();
        }
    }

    return stats;
}

// ============================================================================
// Hot-reload
// ============================================================================

void AssetManager::onMetaFileChanged(const std::string& metaPath)
{
    // Derive asset path from meta path: strip ".ya-meta.json" suffix
    const std::string suffix = ".ya-meta.json";
    if (metaPath.size() <= suffix.size() || !metaPath.ends_with(suffix)) {
        YA_CORE_WARN("onMetaFileChanged: '{}' does not look like a .ya-meta.json file", metaPath);
        return;
    }

    std::string assetPath = metaPath.substr(0, metaPath.size() - suffix.size());

    // Reload meta (copy oldMeta by value — reloadMeta erases from _metaCache)
    AssetMeta        oldMeta = getOrLoadMeta(assetPath);
    const AssetMeta& newMeta = reloadMeta(assetPath);

    if (oldMeta == newMeta) {
        YA_CORE_TRACE("onMetaFileChanged: meta unchanged for '{}'", assetPath);
        return;
    }

    YA_CORE_INFO("onMetaFileChanged: meta changed for '{}', reloading affected resources", assetPath);

    // Find and defer-delete all cache entries for this asset path
    auto&    ddq   = DeferredDeletionQueue::get();
    uint64_t frame = getCurrentFrameIdx();

    auto keys = findCacheKeysForPath(assetPath);
    for (auto& key : keys) {
        // Move GPU resources into deferred queue before erasing
        auto texIt = _textureViews.find(key);
        if (texIt != _textureViews.end()) {
            ddq.enqueueResource(frame, std::move(texIt->second));
            _textureViews.erase(texIt);
        }
        auto modelIt = modelCache.find(key);
        if (modelIt != modelCache.end()) {
            ddq.enqueueResource(frame, std::move(modelIt->second));
            modelCache.erase(modelIt);
        }
    }

    // Reload with new meta
    if (newMeta.type == "texture") {
        loadTexture(assetPath);
    }
    else if (newMeta.type == "model") {
        loadModel(assetPath);
    }

    // Bump version so all TAssetRef instances detect the change
    bumpResourceVersion(assetPath);
}

void AssetManager::onAssetFileChanged(const std::string& assetPath)
{
    YA_CORE_INFO("onAssetFileChanged: '{}' changed, reloading", assetPath);

    // Defer-delete all cache entries for this path
    auto&    ddq   = DeferredDeletionQueue::get();
    uint64_t frame = getCurrentFrameIdx();

    auto keys = findCacheKeysForPath(assetPath);
    for (auto& key : keys) {
        auto texIt = _textureViews.find(key);
        if (texIt != _textureViews.end()) {
            ddq.enqueueResource(frame, std::move(texIt->second));
            _textureViews.erase(texIt);
        }
        auto modelIt = modelCache.find(key);
        if (modelIt != modelCache.end()) {
            ddq.enqueueResource(frame, std::move(modelIt->second));
            modelCache.erase(modelIt);
        }
    }

    // Also invalidate meta cache (in case the file type changed, unlikely but safe)
    _metaCache.erase(assetPath);

    // Reload
    AssetMeta meta = getOrLoadMeta(assetPath);
    if (meta.type == "texture") {
        loadTexture(assetPath);
    }
    else if (meta.type == "model") {
        loadModel(assetPath);
    }

    bumpResourceVersion(assetPath);
}
// Query methods
// ============================================================================

std::shared_ptr<Texture> AssetManager::getTextureByPath(const std::string& filepath) const
{
    // Try composite key lookup for all cached metas
    auto metaIt = _metaCache.find(filepath);
    if (metaIt != _metaCache.end()) {
        std::string cacheKey = makeCacheKey(filepath, metaIt->second);
        auto it = _textureViews.find(cacheKey);
        if (it != _textureViews.end()) return it->second;
    }

    // Fallback: linear scan for path prefix match
    for (auto& [key, tex] : _textureViews) {
        if (key.starts_with(filepath)) {
            return tex;
        }
    }

    return nullptr;
}

std::shared_ptr<Texture> AssetManager::getTextureByName(const std::string& name) const
{
    auto nameIt = _textureName2Path.find(name);
    if (nameIt != _textureName2Path.end()) {
        auto texIt = _textureViews.find(nameIt->second);
        if (texIt != _textureViews.end()) {
            return texIt->second;
        }
    }
    return nullptr;
}

bool AssetManager::isTextureLoaded(const std::string& filepath) const
{
    // Check composite key
    auto metaIt = _metaCache.find(filepath);
    if (metaIt != _metaCache.end()) {
        std::string cacheKey = makeCacheKey(filepath, metaIt->second);
        if (_textureViews.find(cacheKey) != _textureViews.end()) return true;
    }

    // Fallback: check raw filepath key (legacy)
    return _textureViews.find(filepath) != _textureViews.end();
}

bool AssetManager::isTextureLoadedByName(const std::string& name) const
{
    return _textureName2Path.find(name) != _textureName2Path.end();
}

void AssetManager::registerTexture(const std::string& name, const stdptr<Texture>& texture)
{
    auto it = _textureViews.find(name);
    if (it != _textureViews.end()) {
        YA_CORE_WARN("Texture with name '{}' already registered. Overwriting.", name);
    }
    _textureViews.insert({name, texture});
}

void AssetManager::invalidate(const std::string& filepath)
{
    auto&    ddq   = DeferredDeletionQueue::get();
    uint64_t frame = getCurrentFrameIdx();

    // Defer-delete all cache keys matching this filepath
    auto keys = findCacheKeysForPath(filepath);
    for (auto& key : keys) {
        auto texIt = _textureViews.find(key);
        if (texIt != _textureViews.end()) {
            ddq.enqueueResource(frame, std::move(texIt->second));
            _textureViews.erase(texIt);
        }
        auto modelIt = modelCache.find(key);
        if (modelIt != modelCache.end()) {
            ddq.enqueueResource(frame, std::move(modelIt->second));
            modelCache.erase(modelIt);
        }
    }

    // Also try exact match (legacy keys)
    {
        auto texIt = _textureViews.find(filepath);
        if (texIt != _textureViews.end()) {
            ddq.enqueueResource(frame, std::move(texIt->second));
            _textureViews.erase(texIt);
        }
        auto modelIt = modelCache.find(filepath);
        if (modelIt != modelCache.end()) {
            ddq.enqueueResource(frame, std::move(modelIt->second));
            modelCache.erase(modelIt);
        }
    }

    // Clear meta + cacheKey caches for this asset
    _metaCache.erase(filepath);
    _cacheKeyCache.erase(filepath);

    bumpResourceVersion(filepath);
}

std::vector<std::string> AssetManager::findCacheKeysForPath(const std::string& filepath) const
{
    std::vector<std::string> result;
    std::string prefix = filepath + "|";

    for (auto& [key, _] : _textureViews) {
        if (key.starts_with(prefix) || key == filepath) {
            result.push_back(key);
        }
    }
    for (auto& [key, _] : modelCache) {
        if (key.starts_with(prefix) || key == filepath) {
            result.push_back(key);
        }
    }

    return result;
}

// ============================================================================
// Model loading — async by default
// ============================================================================

ModelFuture AssetManager::loadModel(const std::string& filepath)
{
    // Already loaded?
    if (isModelLoaded(filepath)) {
        return ModelFuture(modelCache[filepath]);
    }

    // Submit async decode if not already in flight
    if (!isModelLoadPending(filepath)) {
        submitModelLoad(filepath);
    }

    return ModelFuture();
}

ModelFuture AssetManager::loadModel(const std::string& name, const std::string& filepath)
{
    if (isModelLoaded(filepath)) {
        return ModelFuture(modelCache[filepath]);
    }

    if (!isModelLoadPending(filepath)) {
        submitModelLoad(filepath, name);
    }

    _modalName2Path[name] = filepath;
    return ModelFuture();
}

// ── Sync model loading ──────────────────────────────────────────────────

std::shared_ptr<Model> AssetManager::loadModelSync(const std::string& filepath)
{
    if (isModelLoaded(filepath)) {
        return modelCache[filepath];
    }

    return loadModelImpl(filepath, "");
}

std::shared_ptr<Model> AssetManager::loadModelSync(const std::string& name, const std::string& filepath)
{
    if (isModelLoaded(filepath)) {
        return modelCache[filepath];
    }

    auto model = loadModelImpl(filepath, name);
    if (model) {
        _modalName2Path[name] = filepath;
    }
    return model;
}

// ── Async model pipeline ────────────────────────────────────────────────

void AssetManager::submitModelLoad(const std::string& filepath, const std::string& name)
{
    YA_CORE_INFO("submitModelLoad: async decode '{}'", filepath);

    auto handle = TaskQueue::get().submitWithCallback(
        // ── Worker thread: CPU-only Assimp decode ───────────────────
        [filepath]() -> DecodedModelData {
            return DecodedModelData::decode(filepath);
        },
        // ── Main thread callback: GPU mesh creation ─────────────────
        [this, filepath, name](DecodedModelData decoded) {
            // Race check
            {
                std::lock_guard lock(_cacheMutex);
                if (modelCache.find(filepath) != modelCache.end()) {
                    _pendingModelLoads.erase(filepath);
                    return;
                }
            }

            if (!decoded.isValid()) {
                YA_CORE_WARN("Async model decode failed for '{}'", filepath);
                std::lock_guard lock(_cacheMutex);
                _pendingModelLoads.erase(filepath);
                return;
            }

            // GPU mesh creation on main thread
            auto model = decoded.createModel();
            if (!model) {
                YA_CORE_WARN("Async GPU mesh creation failed for '{}'", filepath);
                std::lock_guard lock(_cacheMutex);
                _pendingModelLoads.erase(filepath);
                return;
            }

            {
                std::lock_guard lock(_cacheMutex);
                modelCache[filepath] = model;
                if (!name.empty()) {
                    _modalName2Path[name] = filepath;
                }
                _pendingModelLoads.erase(filepath);
            }

            YA_CORE_INFO("Async model ready: '{}' ({} meshes, {} materials)",
                          filepath, model->meshes.size(), model->embeddedMaterials.size());
        });

    std::lock_guard lock(_cacheMutex);
    _pendingModelLoads[filepath] = std::move(handle);
}

bool AssetManager::isModelLoadPending(const std::string& filepath) const
{
    std::lock_guard lock(_cacheMutex);
    auto it = _pendingModelLoads.find(filepath);
    if (it == _pendingModelLoads.end()) return false;
    return !it->second.isReady();
}

bool AssetManager::isModelLoaded(const std::string& filepath) const
{
    return modelCache.find(filepath) != modelCache.end();
}

std::shared_ptr<Model> AssetManager::getModel(const std::string& filepath) const
{
    if (isModelLoaded(filepath)) {
        return modelCache.at(filepath);
    }
    return nullptr;
}

std::shared_ptr<Model> AssetManager::loadModelImpl(const std::string& filepath, const std::string& identifier)
{
    // Check if the model is already loaded
    if (isModelLoaded(filepath)) {
        return modelCache[filepath];
    }

    // Synchronous path: decode + GPU mesh creation on calling thread
    auto decoded = DecodedModelData::decode(filepath);
    if (!decoded.isValid()) {
        YA_CORE_ERROR("loadModelImpl: Failed to decode model: {}", filepath);
        return nullptr;
    }

    auto model = decoded.createModel();
    if (!model) {
        YA_CORE_ERROR("loadModelImpl: Failed to create GPU meshes for: {}", filepath);
        return nullptr;
    }

    // Cache the model
    modelCache[filepath] = model;
    return model;
}

} // namespace ya
