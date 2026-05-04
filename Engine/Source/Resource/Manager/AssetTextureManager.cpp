#include "Resource/Manager/AssetTextureManager.h"

#include "Resource/Texture/AssetTextureInternal.h"

#include "Core/Log.h"

#include <atomic>

#include "Resource/DeferredDeletionQueue.h"

namespace ya
{
using namespace asset_manager_texture_detail;

AssetTextureManager::AssetTextureManager(AssetManager& owner)
    : _owner(owner)
{
}

void AssetTextureManager::clear()
{
    std::lock_guard lock(_mutex);
    _textureViews.clear();
    _textureName2Path.clear();
    _pendingTextureLoads.clear();
    _failedTextureLoads.clear();
    _pendingTextureCallbacks.clear();
    _pendingTextureBatchMemoryLoads.clear();
    _readyTextureBatchMemory.clear();
    _nextTextureBatchMemoryHandle = 1;
}

TextureFuture AssetManager::loadTexture(const TextureLoadRequest& request)
{
    return textureManager().loadTexture(request);
}

void AssetManager::loadTextureBatch(const TextureBatchLoadRequest& request)
{
    textureManager().loadTextureBatch(request);
}

AssetManager::TextureBatchMemoryHandle AssetManager::loadTextureBatchIntoMemory(
    const TextureBatchMemoryLoadRequest& request)
{
    return textureManager().loadTextureBatchIntoMemory(request);
}

bool AssetManager::consumeTextureBatchMemory(TextureBatchMemoryHandle handle,
                                             TextureBatchMemory&      outBatchMemory)
{
    return textureManager().consumeTextureBatchMemory(handle, outBatchMemory);
}

std::shared_ptr<Texture> AssetManager::loadTextureSync(const std::string& name,
                                                       const std::string& filepath,
                                                       ETextureColorSpace colorSpace)
{
    return textureManager().loadTextureSync(name, filepath, colorSpace);
}

TextureFuture AssetTextureManager::loadTexture(const AssetManager::TextureLoadRequest& request)
{
    if (request.filepath.empty()) {
        return TextureFuture();
    }

    AssetManager::TextureLoadRequest normalized = request;
    normalized.filepath = AssetManager::normalizeAssetPath(normalized.filepath);

    if (normalized.textureSemantic.has_value()) {
        normalized.colorSpace = AssetManager::inferTextureColorSpace(*normalized.textureSemantic);
        normalized.textureSemantic.reset();
        return loadTexture(normalized);
    }

    const auto settings = _owner.resolveTextureImportSettings(normalized.filepath, normalized.colorSpace);
    const auto cacheKey = _owner.buildTextureCacheKey(normalized.filepath, settings);

    {
        std::lock_guard lock(_mutex);
        const auto      it = _textureViews.find(cacheKey);
        if (it != _textureViews.end()) {
            if (!request.name.empty()) {
                _textureName2Path[normalized.name] = cacheKey;
            }
            if (normalized.onReady) {
                AssetManager::dispatchToGameThread([onReady = normalized.onReady, texture = it->second]() mutable
                                                   { onReady(texture); });
            }
            return TextureFuture(it->second);
        }
    }

    if (isTextureLoadFailed(normalized.filepath)) {
        if (normalized.onReady) {
            AssetManager::dispatchToGameThread([onReady = normalized.onReady]() mutable
                                               { onReady(nullptr); });
        }
        return TextureFuture();
    }

    if (!isTextureLoadPending(cacheKey)) {
        submitTextureLoad(normalized.filepath, cacheKey, settings, normalized.name);
    }

    if (!normalized.name.empty()) {
        std::lock_guard lock(_mutex);
        _textureName2Path[normalized.name] = cacheKey;
    }

    if (normalized.onReady) {
        registerTextureCallback(cacheKey, normalized.onReady);
    }

    return TextureFuture();
}

void AssetTextureManager::loadTextureBatch(const AssetManager::TextureBatchLoadRequest& request)
{
    auto        filepaths  = request.filepaths;
    for (auto& filepath : filepaths) {
        filepath = AssetManager::normalizeAssetPath(filepath);
    }
    auto        onDone     = request.onDone;
    const auto  colorSpace = request.colorSpace;

    if (filepaths.empty()) {
        AssetManager::dispatchToGameThread([onDone = std::move(onDone)]() mutable
                                           { onDone({}); });
        return;
    }

    struct BatchState
    {
        std::mutex                              mutex;
        std::vector<std::shared_ptr<Texture>>   textures;
        std::atomic<uint32_t>                   remaining = 0;
        AssetManager::TextureBatchReadyCallback onDone;
    };

    auto batchState = std::make_shared<BatchState>();
    batchState->textures.resize(filepaths.size());
    batchState->remaining.store(static_cast<uint32_t>(filepaths.size()), std::memory_order_release);
    batchState->onDone = std::move(onDone);

    for (size_t index = 0; index < filepaths.size(); ++index) {
        const auto& filepath = filepaths[index];
        loadTexture(AssetManager::TextureLoadRequest{
            .filepath = filepath,
            .name     = {},
            .onReady  = [batchState, index](const std::shared_ptr<Texture>& texture)
            {
                {
                    std::lock_guard lock(batchState->mutex);
                    batchState->textures[index] = texture;
                }

                if (batchState->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    auto textures = batchState->textures;
                    auto onDone   = batchState->onDone;
                    if (onDone) {
                        onDone(textures);
                    }
                }
            },
            .colorSpace      = colorSpace,
            .textureSemantic = {},
        });
    }
}

AssetManager::TextureBatchMemoryHandle AssetTextureManager::loadTextureBatchIntoMemory(
    const AssetManager::TextureBatchMemoryLoadRequest& request)
{
    auto                                                    filepaths  = request.filepaths;
    for (auto& filepath : filepaths) {
        filepath = AssetManager::normalizeAssetPath(filepath);
    }
    const auto                                               colorSpace = request.colorSpace;
    std::vector<AssetManager::ResolvedTextureImportSettings> settingsList;
    settingsList.reserve(filepaths.size());

    for (const auto& filepath : filepaths) {
        settingsList.push_back(_owner.resolveTextureImportSettings(filepath, colorSpace));
    }

    AssetManager::TextureBatchMemoryHandle handleId = 0;
    {
        std::lock_guard lock(_mutex);
        handleId = _nextTextureBatchMemoryHandle++;
    }

    if (filepaths.empty()) {
        std::lock_guard lock(_mutex);
        _readyTextureBatchMemory.emplace(handleId, AssetManager::TextureBatchMemory{});
        return handleId;
    }

    auto handle = TaskQueue::get().submitWithCallback(
        [settingsList = std::move(settingsList)]() -> AssetManager::TextureBatchMemory
        {
            AssetManager::TextureBatchMemory batchMemory;
            batchMemory.textures.reserve(settingsList.size());

            for (const auto& settings : settingsList) {
                auto decoded = decodeTextureToMemory(settings);
                batchMemory.textures.push_back(std::move(decoded));
            }

            return batchMemory;
        },
        [this, handleId](AssetManager::TextureBatchMemory batchMemory)
        {
            std::lock_guard lock(_mutex);
            _pendingTextureBatchMemoryLoads.erase(handleId);
            _readyTextureBatchMemory[handleId] = std::move(batchMemory);
        });

    {
        std::lock_guard lock(_mutex);
        _pendingTextureBatchMemoryLoads[handleId] = std::move(handle);
    }

    return handleId;
}

bool AssetTextureManager::consumeTextureBatchMemory(AssetManager::TextureBatchMemoryHandle handle,
                                                    AssetManager::TextureBatchMemory&      outBatchMemory)
{
    std::lock_guard lock(_mutex);

    auto readyIt = _readyTextureBatchMemory.find(handle);
    if (readyIt != _readyTextureBatchMemory.end()) {
        outBatchMemory = std::move(readyIt->second);
        _readyTextureBatchMemory.erase(readyIt);
        return true;
    }

    return false;
}

std::shared_ptr<Texture> AssetTextureManager::loadTextureSync(const std::string&               name,
                                                              const std::string&               filepath,
                                                              AssetManager::ETextureColorSpace colorSpace)
{
    const auto normalizedFilepath = AssetManager::normalizeAssetPath(filepath);
    const auto settings = _owner.resolveTextureImportSettings(normalizedFilepath, colorSpace);
    const auto cacheKey = _owner.buildTextureCacheKey(normalizedFilepath, settings);

    {
        std::lock_guard lock(_mutex);
        const auto      it = _textureViews.find(cacheKey);
        if (it != _textureViews.end()) {
            if (!name.empty()) {
                _textureName2Path[name] = cacheKey;
            }
            return it->second;
        }
    }

    if (isTextureLoadFailed(normalizedFilepath)) {
        YA_CORE_WARN("loadTextureSync: Skipping known failed texture: {}", normalizedFilepath);
        return nullptr;
    }

    auto decoded = decodeTextureToMemory(settings);
    if (!decoded.isValid()) {
        if (decoded.hardFailure) {
            rememberTextureLoadFailure(normalizedFilepath, decoded.error.empty() ? "decode failed" : decoded.error);
        }
        YA_CORE_WARN("loadTextureSync: Failed to decode texture: {}", normalizedFilepath);
        return nullptr;
    }

    std::shared_ptr<Texture> texture;
    try {
        texture = Texture::fromMemory(TextureMemoryCreateInfo{
            .filepath = decoded.filepath,
            .label    = name.empty() ? decoded.filepath : name,
            .memory   = TextureMemoryView{
                .width     = decoded.width,
                .height    = decoded.height,
                .channels  = decoded.channels,
                .mipLevels = decoded.mipLevels,
                .format    = decoded.format,
                .data      = decoded.data(),
                .dataSize  = decoded.dataSize(),
            },
        });
    }
    catch (const std::exception& e) {
        YA_CORE_WARN("loadTextureSync: GPU upload failed for '{}' with exception: {}", normalizedFilepath, e.what());
        return nullptr;
    }
    catch (...) {
        YA_CORE_WARN("loadTextureSync: GPU upload failed for '{}' with unknown exception", normalizedFilepath);
        return nullptr;
    }
    if (!texture) {
        YA_CORE_WARN("loadTextureSync: Failed to create texture: {}", normalizedFilepath);
        return nullptr;
    }

    clearTextureLoadFailure(normalizedFilepath);

    {
        std::lock_guard lock(_mutex);
        _textureViews[cacheKey] = texture;
        if (!name.empty()) {
            _textureName2Path[name] = cacheKey;
        }
    }
    return texture;
}

void AssetTextureManager::submitTextureLoad(const std::string&                          filepath,
                                            const std::string&                          cacheKey,
                                            AssetManager::ResolvedTextureImportSettings settings,
                                            const std::string&                          name)
{
    YA_CORE_TRACE("submitTextureLoad: async decode '{}' (format={}, payload={})",
                 filepath,
                 static_cast<int>(settings.resolvedFormat),
                 AssetManager::texturePayloadTypeName(settings.payloadType));

    auto handle = TaskQueue::get().submitWithCallback(
        [settings]() -> AssetManager::TextureMemoryBlock
        {
            return decodeTextureToMemory(settings);
        },
        [this, filepath, cacheKey, name](AssetManager::TextureMemoryBlock decoded)
        {
            std::vector<AssetManager::TextureReadyCallback> callbacks;
            std::shared_ptr<Texture>                        readyTexture;

            {
                std::lock_guard lock(_mutex);
                auto            existing = _textureViews.find(cacheKey);
                if (existing != _textureViews.end()) {
                    _pendingTextureLoads.erase(cacheKey);
                    callbacks    = takeTextureCallbacks(cacheKey);
                    readyTexture = existing->second;
                }
            }

            if (readyTexture) {
                dispatchTextureCallbacks(callbacks, readyTexture);
                return;
            }

            if (!decoded.isValid()) {
                YA_CORE_WARN("Async texture decode failed for '{}'", filepath);
                {
                    std::lock_guard lock(_mutex);
                    _pendingTextureLoads.erase(cacheKey);
                    callbacks = takeTextureCallbacks(cacheKey);
                }
                if (decoded.hardFailure) {
                    rememberTextureLoadFailure(filepath, decoded.error.empty() ? "decode failed" : decoded.error);
                }
                dispatchTextureCallbacks(callbacks, nullptr);
                return;
            }

            std::shared_ptr<Texture> texture;
            try {
                texture = Texture::fromMemory(TextureMemoryCreateInfo{
                    .filepath = decoded.filepath,
                    .label    = decoded.filepath,
                    .memory   = TextureMemoryView{
                        .width     = decoded.width,
                        .height    = decoded.height,
                        .channels  = decoded.channels,
                        .mipLevels = decoded.mipLevels,
                        .format    = decoded.format,
                        .data      = decoded.data(),
                        .dataSize  = decoded.dataSize(),
                    },
                });
            }
            catch (const std::exception& e) {
                YA_CORE_WARN("Async GPU upload failed for '{}' with exception: {}", filepath, e.what());
            }
            catch (...) {
                YA_CORE_WARN("Async GPU upload failed for '{}' with unknown exception", filepath);
            }
            if (!texture) {
                YA_CORE_WARN("Async GPU upload failed for '{}'", filepath);
                {
                    std::lock_guard lock(_mutex);
                    _pendingTextureLoads.erase(cacheKey);
                    callbacks = takeTextureCallbacks(cacheKey);
                }
                dispatchTextureCallbacks(callbacks, nullptr);
                return;
            }

            clearTextureLoadFailure(filepath);

            {
                std::lock_guard lock(_mutex);
                _textureViews[cacheKey] = texture;
                if (!name.empty()) {
                    _textureName2Path[name] = cacheKey;
                }
                _pendingTextureLoads.erase(cacheKey);
                callbacks = takeTextureCallbacks(cacheKey);
            }

            dispatchTextureCallbacks(callbacks, texture);

            YA_CORE_TRACE("Async texture ready: '{}' ({}x{})", filepath, texture->getWidth(), texture->getHeight());
        });

    std::lock_guard lock(_mutex);
    _pendingTextureLoads[cacheKey] = std::move(handle);
}

std::shared_ptr<Texture> AssetTextureManager::getTextureByPath(const std::string& filepath) const
{
    const auto normalizedFilepath = AssetManager::normalizeAssetPath(filepath);
    std::lock_guard lock(_mutex);

    auto metaIt = _owner._metaCache.find(normalizedFilepath);
    if (metaIt != _owner._metaCache.end()) {
        const std::string cacheKey = AssetManager::makeCacheKey(normalizedFilepath, metaIt->second);
        auto              it       = _textureViews.find(cacheKey);
        if (it != _textureViews.end()) {
            return it->second;
        }
    }

    for (const auto& [key, texture] : _textureViews) {
        if (key.starts_with(normalizedFilepath + "|")) {
            return texture;
        }
    }

    return nullptr;
}

std::shared_ptr<Texture> AssetTextureManager::getTextureByName(const std::string& name) const
{
    std::lock_guard lock(_mutex);
    auto            nameIt = _textureName2Path.find(name);
    if (nameIt == _textureName2Path.end()) {
        return nullptr;
    }

    auto texIt = _textureViews.find(nameIt->second);
    return texIt != _textureViews.end() ? texIt->second : nullptr;
}

bool AssetTextureManager::isTextureLoaded(const std::string& filepath) const
{
    return getTextureByPath(filepath) != nullptr;
}

bool AssetTextureManager::isTextureLoadedByName(const std::string& name) const
{
    return getTextureByName(name) != nullptr;
}

void AssetTextureManager::registerTexture(const std::string& name, const stdptr<Texture>& texture)
{
    if (!texture) {
        return;
    }

    std::lock_guard lock(_mutex);
    _textureViews[name]     = texture;
    _textureName2Path[name] = name;
}

bool AssetTextureManager::isTextureLoadPending(const std::string& cacheKey) const
{
    std::lock_guard lock(_mutex);
    auto            it = _pendingTextureLoads.find(cacheKey);
    return it != _pendingTextureLoads.end() && !it->second.isReady();
}

bool AssetTextureManager::isTextureLoadFailed(const std::string& filepath) const
{
    const auto normalizedFilepath = AssetManager::normalizeAssetPath(filepath);
    std::lock_guard lock(_mutex);
    return _failedTextureLoads.contains(normalizedFilepath);
}

size_t AssetTextureManager::collectUnused(uint64_t frame)
{
    size_t          released = 0;
    auto&           ddq      = DeferredDeletionQueue::get();
    std::lock_guard lock(_mutex);
    for (auto it = _textureViews.begin(); it != _textureViews.end();) {
        if (it->second && it->second.use_count() == 1) {
            ddq.enqueueResource(frame, std::move(it->second));
            it = _textureViews.erase(it);
            ++released;
        }
        else {
            ++it;
        }
    }
    return released;
}

bool AssetTextureManager::unload(const std::string& cacheKey, uint64_t frame)
{
    const auto normalizedCacheKey = AssetManager::normalizeAssetPath(cacheKey);
    std::lock_guard lock(_mutex);
    auto            it = _textureViews.find(normalizedCacheKey);
    if (it == _textureViews.end()) {
        return false;
    }

    DeferredDeletionQueue::get().enqueueResource(frame, std::move(it->second));
    _textureViews.erase(it);
    return true;
}

void AssetTextureManager::invalidate(const std::string& filepath, uint64_t frame)
{
    evictCachedAsset(filepath, frame);
}

void AssetTextureManager::evictCachedAsset(const std::string& assetPath, uint64_t frame)
{
    const auto normalizedAssetPath = AssetManager::normalizeAssetPath(assetPath);
    auto&           ddq = DeferredDeletionQueue::get();
    std::lock_guard lock(_mutex);
    for (auto it = _textureViews.begin(); it != _textureViews.end();) {
        if (it->first == normalizedAssetPath || it->first.starts_with(normalizedAssetPath + "|")) {
            ddq.enqueueResource(frame, std::move(it->second));
            it = _textureViews.erase(it);
        }
        else {
            ++it;
        }
    }
}

void AssetTextureManager::fillStats(AssetManager::CacheStats& stats) const
{
    std::lock_guard lock(_mutex);
    stats.textureCount += _textureViews.size();
    for (const auto& [_, texture] : _textureViews) {
        if (texture) {
            stats.textureMemoryEstimate += static_cast<size_t>(texture->getWidth()) *
                                           texture->getHeight() *
                                           std::max(texture->getChannels(), 1u);
        }
    }
}

void AssetTextureManager::registerTextureCallback(const std::string& cacheKey, AssetManager::TextureReadyCallback onReady)
{
    if (!onReady) {
        return;
    }

    std::shared_ptr<Texture> readyTexture;
    {
        std::lock_guard lock(_mutex);
        auto            loadedIt = _textureViews.find(cacheKey);
        if (loadedIt != _textureViews.end()) {
            readyTexture = loadedIt->second;
        }
        else {
            _pendingTextureCallbacks[cacheKey].push_back(std::move(onReady));
            return;
        }
    }

    AssetManager::dispatchToGameThread([onReady = std::move(onReady), readyTexture]() mutable
                                       { onReady(readyTexture); });
}

std::vector<AssetManager::TextureReadyCallback> AssetTextureManager::takeTextureCallbacks(const std::string& cacheKey)
{
    auto it = _pendingTextureCallbacks.find(cacheKey);
    if (it == _pendingTextureCallbacks.end()) {
        return {};
    }

    auto callbacks = std::move(it->second);
    _pendingTextureCallbacks.erase(it);
    return callbacks;
}

void AssetTextureManager::dispatchTextureCallbacks(const std::vector<AssetManager::TextureReadyCallback>& callbacks,
                                                   const std::shared_ptr<Texture>&                        texture)
{
    for (const auto& callback : callbacks) {
        if (!callback) {
            continue;
        }

        AssetManager::dispatchToGameThread([callback, texture]()
                                           { callback(texture); });
    }
}

void AssetTextureManager::rememberTextureLoadFailure(const std::string& filepath, std::string reason)
{
    const auto normalizedFilepath = AssetManager::normalizeAssetPath(filepath);
    std::lock_guard lock(_mutex);
    _failedTextureLoads[normalizedFilepath] = std::move(reason);
}

void AssetTextureManager::clearTextureLoadFailure(const std::string& filepath)
{
    const auto normalizedFilepath = AssetManager::normalizeAssetPath(filepath);
    std::lock_guard lock(_mutex);
    _failedTextureLoads.erase(normalizedFilepath);
}

} // namespace ya