#include "Resource/Manager/AssetTextureManager.h"

#include "Resource/Texture/AssetTextureInternal.h"

#include "Core/Log.h"

#include <array>
#include <atomic>

#include "Core/Common/DeferredDeletionQueue.h"

namespace ya
{
using namespace asset_manager_texture_detail;

namespace
{
constexpr std::array kTextureColorSpaces = {
    AssetManager::ETextureColorSpace::SRGB,
    AssetManager::ETextureColorSpace::Linear,
};
} // namespace

AssetTextureManager::AssetTextureManager(AssetManager& owner)
    : _owner(owner)
{
    auto unavailable   = std::make_shared<AssetSlot<Texture>>();
    unavailable->state = EAssetSlotState::Failed;
    _unavailableSlot   = std::move(unavailable);
}

std::string AssetTextureManager::requestKey(const std::string& normalizedPath, AssetManager::ETextureColorSpace colorSpace)
{
    return normalizedPath + "|" + AssetManager::textureColorSpaceName(colorSpace);
}

void AssetTextureManager::clear()
{
    std::vector<std::function<void()>> updateObservers;
    {
        std::lock_guard lock(_mutex);
        ++_clearGeneration;
        // Refs outlive the backend teardown that calls this; leave their slots
        // Failed instead of pointing at destroyed GPU objects.
        for (auto& [key, entry] : _entries) {
            (void)key;
            entry.slot->resource.reset();
            entry.slot->state = EAssetSlotState::Failed;
            ++entry.slot->generation;
            auto observers = entry.slot->observers.gather();
            updateObservers.insert(updateObservers.end(),
                                 std::make_move_iterator(observers.begin()),
                                 std::make_move_iterator(observers.end()));
        }
        _entries.clear();
        _textureName2Key.clear();
        _pendingTextureBatchMemoryLoads.clear();
        _readyTextureBatchMemory.clear();
        _nextTextureBatchMemoryHandle = 1;
    }
    // Slots went Failed with a generation bump; subscribers hear the
    // update outside the lock. Teardown-path callbacks only enqueue or drop state.
    for (auto& observer : updateObservers) {
        observer();
    }
}

AssetHandle<Texture> AssetManager::loadTexture(const TextureLoadRequest& request)
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

AssetHandle<Texture> AssetTextureManager::loadTexture(const AssetManager::TextureLoadRequest& request)
{
    if (request.filepath.empty()) {
        return nullptr;
    }

    if (!_owner.getRender()) {
        if (request.onReady) {
            AssetManager::dispatchToGameThread([onReady = request.onReady]() mutable
                                               { onReady(nullptr); });
        }
        return _unavailableSlot;
    }

    const std::string path       = AssetManager::normalizeAssetPath(request.filepath);
    const auto        colorSpace = request.textureSemantic.has_value()
                                       ? AssetManager::inferTextureColorSpace(*request.textureSemantic)
                                       : request.colorSpace;
    const std::string key        = requestKey(path, colorSpace);

    AssetHandle<Texture>     handle;
    std::shared_ptr<Texture> settledTexture;
    bool                     bSettled  = false;
    bool                     bInserted = false;
    {
        std::lock_guard lock(_mutex);
        if (!request.name.empty()) {
            _textureName2Key[FName(request.name)] = key;
        }
        auto [it, inserted] = _entries.try_emplace(key);
        TextureEntry& entry = it->second;
        bInserted           = inserted;
        if (inserted) {
            entry.slot       = std::make_shared<AssetSlot<Texture>>();
            entry.filepath   = path;
            entry.colorSpace = colorSpace;
        }
        handle = entry.slot;
        if (entry.slot->state == EAssetSlotState::Loading) {
            if (request.onReady) {
                entry.readyCallbacks.push_back(request.onReady);
            }
        }
        else {
            bSettled       = true;
            settledTexture = entry.slot->resource;
        }
    }

    if (bInserted) {
        submitTextureLoad(key, path, colorSpace);
    }
    if (bSettled && request.onReady) {
        AssetManager::dispatchToGameThread([onReady = request.onReady, settledTexture]() mutable
                                           { onReady(settledTexture); });
    }
    return handle;
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

    AssetManager::TextureBatchMemoryHandle handleId         = 0;
    uint64_t                               clearGeneration = 0;
    {
        std::lock_guard lock(_mutex);
        handleId         = _nextTextureBatchMemoryHandle++;
        clearGeneration = _clearGeneration;
    }

    auto onReady = request.onReady;

    if (filepaths.empty()) {
        {
            std::lock_guard lock(_mutex);
            _readyTextureBatchMemory.emplace(handleId, AssetManager::TextureBatchMemory{});
        }
        if (onReady) {
            onReady(handleId);
        }
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
        [this, handleId, clearGeneration, onReady = std::move(onReady)](AssetManager::TextureBatchMemory batchMemory)
        {
            bool bStored = false;
            {
                std::lock_guard lock(_mutex);
                _pendingTextureBatchMemoryLoads.erase(handleId);
                if (clearGeneration == _clearGeneration) {
                    _readyTextureBatchMemory[handleId] = std::move(batchMemory);
                    bStored                            = true;
                }
            }
            // Outside the lock: the callback consumes the batch.
            if (bStored && onReady) {
                onReady(handleId);
            }
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
    const std::string path = AssetManager::normalizeAssetPath(filepath);
    const std::string key  = requestKey(path, colorSpace);

    {
        std::lock_guard lock(_mutex);
        if (auto it = _entries.find(key); it != _entries.end() && it->second.slot->state == EAssetSlotState::Ready) {
            if (!name.empty()) {
                _textureName2Key[FName(name)] = key;
            }
            return it->second.slot->resource;
        }
    }

    if (!_owner.getRender()) {
        YA_CORE_WARN("loadTextureSync: Render backend is not available for '{}'", path);
        return nullptr;
    }

    std::shared_ptr<Texture> texture;
    auto                     decoded = decodeTextureToMemory(_owner.resolveTextureImportSettings(path, colorSpace));
    if (decoded.isValid()) {
        texture = uploadTexture(decoded, name.empty() ? decoded.filepath : name);
    }
    else {
        YA_CORE_WARN("loadTextureSync: Failed to decode texture '{}': {}", path, decoded.error);
    }

    SlotUpdate update;
    {
        std::lock_guard lock(_mutex);
        auto [it, inserted] = _entries.try_emplace(key);
        TextureEntry& entry = it->second;
        if (inserted) {
            entry.slot       = std::make_shared<AssetSlot<Texture>>();
            entry.filepath   = path;
            entry.colorSpace = colorSpace;
        }
        // Supersedes any async decode still in flight for this slot.
        entry.loadSerial = ++_nextLoadSerial;
        update           = updateSlotLocked(entry, texture);
        if (!name.empty()) {
            _textureName2Key[FName(name)] = key;
        }
    }
    dispatchSlotUpdate(std::move(update), texture);
    return texture;
}

void AssetTextureManager::submitTextureLoad(const std::string&               key,
                                            const std::string&               filepath,
                                            AssetManager::ETextureColorSpace colorSpace)
{
    // Settings are resolved per submit so a reload picks up edited meta.
    const auto settings = _owner.resolveTextureImportSettings(filepath, colorSpace);
    YA_CORE_TRACE("submitTextureLoad: async decode '{}' (format={}, payload={})",
                  filepath,
                  static_cast<int>(settings.resolvedFormat),
                  AssetManager::texturePayloadTypeName(settings.payloadType));

    uint64_t serial = 0;
    {
        std::lock_guard lock(_mutex);
        auto            it = _entries.find(key);
        if (it == _entries.end()) {
            return;
        }
        serial                = ++_nextLoadSerial;
        it->second.loadSerial = serial;
    }

    TaskQueue::get().submitWithCallback(
        [settings]() -> AssetManager::TextureMemoryBlock
        {
            return decodeTextureToMemory(settings);
        },
        [this, key, serial](AssetManager::TextureMemoryBlock decoded)
        {
            completeTextureLoad(key, serial, std::move(decoded));
        });
}

void AssetTextureManager::completeTextureLoad(const std::string&               key,
                                              uint64_t                         serial,
                                              AssetManager::TextureMemoryBlock decoded)
{
    std::string label;
    {
        std::lock_guard lock(_mutex);
        auto            it = _entries.find(key);
        if (it == _entries.end() || it->second.loadSerial != serial) {
            return;
        }
        label = it->second.filepath;
    }

    std::shared_ptr<Texture> texture;
    if (decoded.isValid()) {
        texture = uploadTexture(decoded, label);
    }
    else {
        YA_CORE_WARN("Async texture decode failed for '{}': {}", label, decoded.error);
    }

    SlotUpdate update;
    {
        std::lock_guard lock(_mutex);
        auto            it = _entries.find(key);
        if (it == _entries.end() || it->second.loadSerial != serial) {
            return;
        }
        update = updateSlotLocked(it->second, texture);
    }
    dispatchSlotUpdate(std::move(update), texture);

    if (texture) {
        YA_CORE_TRACE("Async texture ready: '{}' ({}x{})", label, texture->getWidth(), texture->getHeight());
    }
}

std::shared_ptr<Texture> AssetTextureManager::uploadTexture(const AssetManager::TextureMemoryBlock& decoded,
                                                            const std::string&                      label)
{
    auto* render = _owner.getRender();
    if (!render) {
        return nullptr;
    }

    std::shared_ptr<Texture> texture;
    try {
        texture = Texture::fromMemory(*render, TextureMemoryCreateInfo{
            .filepath = decoded.filepath,
            .label    = label,
            .memory   = TextureMemoryView{
                .width           = decoded.width,
                .height          = decoded.height,
                .channels        = decoded.channels,
                .mipLevels       = decoded.mipLevels,
                .generateMipmaps = decoded.generateMipmaps,
                .format          = decoded.format,
                .data            = decoded.data(),
                .dataSize        = decoded.dataSize(),
            },
        });
    }
    catch (const std::exception& e) {
        YA_CORE_WARN("GPU upload failed for '{}' with exception: {}", label, e.what());
    }
    catch (...) {
        YA_CORE_WARN("GPU upload failed for '{}' with unknown exception", label);
    }
    if (!texture) {
        YA_CORE_WARN("GPU upload failed for '{}'", label);
    }
    return texture;
}

AssetTextureManager::SlotUpdate AssetTextureManager::updateSlotLocked(TextureEntry&                   entry,
                                                                           const std::shared_ptr<Texture>& texture)
{
    AssetSlot<Texture>& slot = *entry.slot;
    if (slot.resource && slot.resource != texture) {
        DeferredDeletionQueue::get().retire(std::move(slot.resource));
    }
    slot.resource = texture;
    slot.state    = texture ? EAssetSlotState::Ready : EAssetSlotState::Failed;
    ++slot.generation;
    SlotUpdate dispatch;
    dispatch.readyCallbacks = std::exchange(entry.readyCallbacks, {});
    dispatch.updateObservers  = slot.observers.gather();
    return dispatch;
}

void AssetTextureManager::dispatchTextureCallbacks(std::vector<AssetManager::TextureReadyCallback> callbacks,
                                                   const std::shared_ptr<Texture>&                 texture)
{
    for (auto& callback : callbacks) {
        if (!callback) {
            continue;
        }
        AssetManager::dispatchToGameThread([callback = std::move(callback), texture]()
                                           { callback(texture); });
    }
}

void AssetTextureManager::dispatchSlotUpdate(SlotUpdate dispatch, const std::shared_ptr<Texture>& texture)
{
    dispatchTextureCallbacks(std::move(dispatch.readyCallbacks), texture);
    // Updates land on the game thread (async completions hop through the
    // TaskQueue main-thread drain); observer callbacks only enqueue work.
    for (auto& observer : dispatch.updateObservers) {
        observer();
    }
}

std::shared_ptr<Texture> AssetTextureManager::getTextureByPath(const std::string& filepath) const
{
    const auto      path = AssetManager::normalizeAssetPath(filepath);
    std::lock_guard lock(_mutex);
    for (const auto colorSpace : kTextureColorSpaces) {
        auto it = _entries.find(requestKey(path, colorSpace));
        if (it != _entries.end() && it->second.slot->state == EAssetSlotState::Ready) {
            return it->second.slot->resource;
        }
    }
    return nullptr;
}

std::shared_ptr<Texture> AssetTextureManager::getTextureByName(const std::string& name) const
{
    std::lock_guard lock(_mutex);
    auto            nameIt = _textureName2Key.find(FName(name));
    if (nameIt == _textureName2Key.end()) {
        return nullptr;
    }
    auto it = _entries.find(nameIt->second);
    if (it == _entries.end() || it->second.slot->state != EAssetSlotState::Ready) {
        return nullptr;
    }
    return it->second.slot->resource;
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

    SlotUpdate update;
    {
        std::lock_guard lock(_mutex);
        auto [it, inserted] = _entries.try_emplace(name);
        TextureEntry& entry = it->second;
        if (inserted) {
            entry.slot = std::make_shared<AssetSlot<Texture>>();
        }
        entry.loadSerial              = ++_nextLoadSerial;
        update                        = updateSlotLocked(entry, texture);
        _textureName2Key[FName(name)] = name;
    }
    dispatchSlotUpdate(std::move(update), texture);
}

bool AssetTextureManager::isTextureLoadFailed(const std::string& filepath) const
{
    const auto      path = AssetManager::normalizeAssetPath(filepath);
    std::lock_guard lock(_mutex);
    for (const auto colorSpace : kTextureColorSpaces) {
        auto it = _entries.find(requestKey(path, colorSpace));
        if (it != _entries.end() && it->second.slot->state == EAssetSlotState::Failed) {
            return true;
        }
    }
    return false;
}

size_t AssetTextureManager::collectUnused(uint64_t frame)
{
    size_t          released = 0;
    auto&           ddq      = DeferredDeletionQueue::get();
    std::lock_guard lock(_mutex);
    for (auto it = _entries.begin(); it != _entries.end();) {
        const auto& slot = it->second.slot;
        // Only the manager holds the slot, and nothing outside the slot still
        // uses the texture (e.g. a material binding kept after its ref left).
        const bool bUnused = slot.use_count() == 1 &&
                             slot->state != EAssetSlotState::Loading &&
                             (!slot->resource || slot->resource.use_count() == 1);
        if (!bUnused) {
            ++it;
            continue;
        }
        if (slot->resource) {
            ddq.enqueueResource(frame, std::move(slot->resource));
        }
        std::erase_if(_textureName2Key, [&](const auto& alias) { return alias.second == it->first; });
        it = _entries.erase(it);
        ++released;
    }
    return released;
}

bool AssetTextureManager::unload(const std::string& filepath, uint64_t frame)
{
    const auto path     = AssetManager::normalizeAssetPath(filepath);
    bool       bRemoved = false;

    SlotUpdate update;
    {
        std::lock_guard lock(_mutex);
        auto&           ddq = DeferredDeletionQueue::get();
        for (const auto colorSpace : kTextureColorSpaces) {
            const std::string key = requestKey(path, colorSpace);
            auto              it  = _entries.find(key);
            if (it == _entries.end()) {
                continue;
            }
            // Refs still holding the slot see it Failed; they load again only
            // when their path is rebound.
            AssetSlot<Texture>& slot = *it->second.slot;
            if (slot.resource) {
                ddq.enqueueResource(frame, std::move(slot.resource));
            }
            auto dispatch = updateSlotLocked(it->second, nullptr);
            update.readyCallbacks.insert(update.readyCallbacks.end(),
                                             std::make_move_iterator(dispatch.readyCallbacks.begin()),
                                             std::make_move_iterator(dispatch.readyCallbacks.end()));
            update.updateObservers.insert(update.updateObservers.end(),
                                          std::make_move_iterator(dispatch.updateObservers.begin()),
                                          std::make_move_iterator(dispatch.updateObservers.end()));
            std::erase_if(_textureName2Key, [&](const auto& alias) { return alias.second == key; });
            _entries.erase(it);
            bRemoved = true;
        }
    }
    dispatchSlotUpdate(std::move(update), nullptr);
    return bRemoved;
}

void AssetTextureManager::reload(const std::string& filepath)
{
    const auto path = AssetManager::normalizeAssetPath(filepath);

    std::vector<std::pair<std::string, AssetManager::ETextureColorSpace>> variants;
    {
        std::lock_guard lock(_mutex);
        for (const auto colorSpace : kTextureColorSpaces) {
            const std::string key = requestKey(path, colorSpace);
            auto              it  = _entries.find(key);
            if (it == _entries.end()) {
                continue;
            }
            // A Ready slot keeps serving the previous texture until the
            // replacement is uploaded; a Failed one retries.
            AssetSlot<Texture>& slot = *it->second.slot;
            if (slot.state == EAssetSlotState::Failed) {
                slot.state = EAssetSlotState::Loading;
            }
            variants.emplace_back(key, colorSpace);
        }
    }
    for (const auto& [key, colorSpace] : variants) {
        submitTextureLoad(key, path, colorSpace);
    }
}

void AssetTextureManager::fillStats(AssetManager::CacheStats& stats) const
{
    std::lock_guard lock(_mutex);
    for (const auto& [key, entry] : _entries) {
        (void)key;
        const auto& texture = entry.slot->resource;
        if (!texture) {
            continue;
        }
        ++stats.textureCount;
        stats.textureMemoryEstimate += static_cast<size_t>(texture->getWidth()) *
                                       texture->getHeight() *
                                       std::max(texture->getChannels(), 1u);
    }
}

} // namespace ya
