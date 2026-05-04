#include "Resource/AssetManager.h"

#include "Resource/Manager/AssetModelManager.h"
#include "Resource/Manager/AssetTextureManager.h"

#include "Core/Debug/Instrumentor.h"
#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Runtime/App/App.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace ya
{
namespace
{
uint64_t getCurrentFrameIdx()
{
    return DeferredDeletionQueue::get().currentFrame();
}
} // namespace

AssetManager* AssetManager::get()
{
    static AssetManager instance;
    return &instance;
}

AssetManager::AssetManager()
    : _textureManager(std::make_unique<AssetTextureManager>(*this))
    , _modelManager(std::make_unique<AssetModelManager>(*this))
{
}

AssetManager::~AssetManager()
{
    YA_CORE_INFO("AssetManager destructor");
}

std::string AssetManager::normalizeAssetPath(std::string path)
{
    if (path.empty()) {
        return path;
    }
    std::replace(path.begin(), path.end(), '\\', '/');
    return std::filesystem::path(path).lexically_normal().generic_string();
}

void AssetManager::clearCache()
{
    YA_PROFILE_FUNCTION_LOG();
    _textureManager->clear();
    _modelManager->clear();
    _metaCache.clear();

    YA_CORE_INFO("AssetManager cleared");
}

const AssetMeta& AssetManager::getOrLoadMeta(const std::string& assetPath)
{
    const std::string normalizedAssetPath = normalizeAssetPath(assetPath);
    {
        auto it = _metaCache.find(normalizedAssetPath);
        if (it != _metaCache.end()) {
            return it->second;
        }
    }

    std::string metaPath = AssetMeta::metaPathFor(normalizedAssetPath);

    AssetMeta meta;
    if (std::filesystem::exists(metaPath)) {
        meta = AssetMeta::loadFromFile(metaPath);
    }
    else {
        auto* vfs = VirtualFileSystem::get();
        auto translatedPath = vfs->translatePath(metaPath).string();
        if (std::filesystem::exists(translatedPath)) {
            meta = AssetMeta::loadFromFile(translatedPath);
        }
        else {
            std::string ext = normalizedAssetPath.substr(normalizedAssetPath.find_last_of('.') + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" ||
                ext == "tga" || ext == "hdr" || ext == "dds" || ext == "ktx" || ext == "ktx2") {
                meta = AssetMeta::defaultForTexture();
            }
            else if (ext == "obj" || ext == "fbx" || ext == "gltf" || ext == "glb" ||
                     ext == "dae" || ext == "blend") {
                meta = AssetMeta::defaultForModel();
            }
            else {
                meta.type = "unknown";
            }

            YA_CORE_TRACE("AssetMeta: no sidecar for '{}', using default (type={})", normalizedAssetPath, meta.type);
        }
    }

    auto [it, inserted] = _metaCache.emplace(normalizedAssetPath, std::move(meta));
    (void)inserted;
    return it->second;
}

const AssetMeta& AssetManager::reloadMeta(const std::string& assetPath)
{
    const std::string normalizedAssetPath = normalizeAssetPath(assetPath);
    _metaCache.erase(normalizedAssetPath);
    return getOrLoadMeta(normalizedAssetPath);
}

std::string AssetManager::makeCacheKey(const std::string& filepath, const AssetMeta& meta)
{
    return normalizeAssetPath(filepath) + "|" + std::to_string(meta.propertiesHash());
}

std::string AssetManager::buildTextureCacheKey(const std::string& requestPath,
                                               const ResolvedTextureImportSettings& settings)
{
    const std::string normalizedRequestPath = normalizeAssetPath(requestPath);
    const auto& meta = getOrLoadMeta(normalizedRequestPath);
    return normalizedRequestPath + "|" +
           normalizeAssetPath(settings.sourceInfo.filepath) + "|" +
           std::to_string(meta.propertiesHash()) + "|" +
           std::to_string(static_cast<int>(settings.colorSpace)) + "|" +
           std::to_string(static_cast<int>(settings.uploadStrategy)) + "|" +
           std::to_string(static_cast<int>(settings.transcodeTarget)) + "|" +
           std::to_string(static_cast<int>(settings.resolvedFormat));
}

AssetTextureManager& AssetManager::textureManager()
{
    return *_textureManager;
}

const AssetTextureManager& AssetManager::textureManager() const
{
    return *_textureManager;
}

AssetModelManager& AssetManager::modelManager()
{
    return *_modelManager;
}

const AssetModelManager& AssetManager::modelManager() const
{
    return *_modelManager;
}

void AssetManager::dispatchToGameThread(std::function<void()> task)
{
    if (!task) {
        return;
    }

    auto* app = App::get();
    if (!app) {
        task();
        return;
    }

    app->taskManager.registerFrameTask(std::move(task));
}

uint64_t AssetManager::getResourceVersion(const std::string& assetPath) const
{
    const auto normalizedAssetPath = normalizeAssetPath(assetPath);
    auto it = _resourceVersion.find(normalizedAssetPath);
    return (it != _resourceVersion.end()) ? it->second : 0;
}

void AssetManager::bumpResourceVersion(const std::string& assetPath)
{
    const auto normalizedAssetPath = normalizeAssetPath(assetPath);
    const auto newVersion = ++_resourceVersion[normalizedAssetPath];
    YA_CORE_TRACE("bumpResourceVersion: '{}' → v{}", normalizedAssetPath, newVersion);
}

bool AssetManager::isTextureLoadPending(const std::string& cacheKey) const
{
    return textureManager().isTextureLoadPending(cacheKey);
}

bool AssetManager::isTextureLoadFailed(const std::string& filepath) const
{
    return textureManager().isTextureLoadFailed(filepath);
}

size_t AssetManager::collectUnused()
{
    const uint64_t frame = getCurrentFrameIdx();
    size_t         released = 0;
    released += textureManager().collectUnused(frame);
    released += modelManager().collectUnused(frame);

    if (released > 0) {
        YA_CORE_INFO("collectUnused: {} resources scheduled for deferred release", released);
    }

    return released;
}

void AssetManager::unload(const std::string& cacheKey)
{
    const uint64_t frame = getCurrentFrameIdx();
    if (textureManager().unload(cacheKey, frame) || modelManager().unload(cacheKey, frame)) {
        return;
    }

    YA_CORE_WARN("unload: cache key '{}' not found", cacheKey);
}

AssetManager::CacheStats AssetManager::getStats() const
{
    CacheStats stats;
    textureManager().fillStats(stats);
    modelManager().fillStats(stats);

    return stats;
}

void AssetManager::onMetaFileChanged(const std::string& metaPath)
{
    const std::string suffix = ".ya-meta.json";
    if (metaPath.size() <= suffix.size() || !metaPath.ends_with(suffix)) {
        YA_CORE_WARN("onMetaFileChanged: '{}' does not look like a .ya-meta.json file", metaPath);
        return;
    }

    std::string assetPath = metaPath.substr(0, metaPath.size() - suffix.size());

    AssetMeta oldMeta = getOrLoadMeta(assetPath);
    const AssetMeta& newMeta = reloadMeta(assetPath);

    if (oldMeta == newMeta) {
        YA_CORE_TRACE("onMetaFileChanged: meta unchanged for '{}'", assetPath);
        return;
    }

    YA_CORE_INFO("onMetaFileChanged: meta changed for '{}', reloading affected resources", assetPath);

    evictCachedAsset(assetPath);

    if (newMeta.type == "texture") {
        loadTexture(TextureLoadRequest{.filepath = assetPath, .name = {}, .onReady = {}, .colorSpace = ETextureColorSpace::SRGB, .textureSemantic = {}});
    }
    else if (newMeta.type == "model") {
        loadModel(ModelLoadRequest{.filepath = assetPath, .name = {}, .onReady = {}});
    }

    bumpResourceVersion(assetPath);
}

void AssetManager::onAssetFileChanged(const std::string& assetPath)
{
    YA_CORE_INFO("onAssetFileChanged: '{}' changed, reloading", assetPath);

    evictCachedAsset(assetPath);
    _metaCache.erase(assetPath);

    AssetMeta meta = getOrLoadMeta(assetPath);
    if (meta.type == "texture") {
        loadTexture(TextureLoadRequest{.filepath = assetPath, .name = {}, .onReady = {}, .colorSpace = ETextureColorSpace::SRGB, .textureSemantic = {}});
    }
    else if (meta.type == "model") {
        loadModel(ModelLoadRequest{.filepath = assetPath, .name = {}, .onReady = {}});
    }

    bumpResourceVersion(assetPath);
}

std::shared_ptr<Texture> AssetManager::getTextureByPath(const std::string& filepath) const
{
    return textureManager().getTextureByPath(normalizeAssetPath(filepath));
}

std::shared_ptr<Texture> AssetManager::getTextureByName(const std::string& name) const
{
    return textureManager().getTextureByName(name);
}

bool AssetManager::isTextureLoaded(const std::string& filepath) const
{
    return textureManager().isTextureLoaded(normalizeAssetPath(filepath));
}

bool AssetManager::isTextureLoadedByName(const std::string& name) const
{
    return textureManager().isTextureLoadedByName(name);
}

void AssetManager::registerTexture(const std::string& name, const stdptr<Texture>& texture)
{
    textureManager().registerTexture(name, texture);
}

void AssetManager::invalidate(const std::string& filepath)
{
    const auto normalizedFilepath = normalizeAssetPath(filepath);
    evictCachedAsset(normalizedFilepath);
    _metaCache.erase(normalizedFilepath);
    bumpResourceVersion(normalizedFilepath);
}

void AssetManager::evictCachedAsset(const std::string& assetPath)
{
    const uint64_t frame = getCurrentFrameIdx();
    textureManager().evictCachedAsset(assetPath, frame);
    modelManager().evictCachedAsset(assetPath, frame);
}

} // namespace ya