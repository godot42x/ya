#include "Resource/Manager/AssetModelManager.h"


#include "Core/Log.h"
#include "Render/Model/ImportedModelData.h"

#include "Resource/DeferredDeletionQueue.h"

namespace ya
{

AssetModelManager::AssetModelManager(AssetManager& owner)
    : _owner(owner)
{
}

void AssetModelManager::clear()
{
    std::lock_guard lock(_mutex);
    modelCache.clear();
    _modalName2Path.clear();
    _pendingModelLoads.clear();
    _pendingModelCallbacks.clear();
    _failedModelLoads.clear();
}

ModelFuture AssetManager::loadModel(const ModelLoadRequest& request)
{
    return modelManager().loadModel(request);
}

std::shared_ptr<Model> AssetManager::loadModelSync(const std::string& filepath)
{
    return modelManager().loadModelSync(filepath);
}

std::shared_ptr<Model> AssetManager::loadModelSync(const std::string& name, const std::string& filepath)
{
    return modelManager().loadModelSync(name, filepath);
}

bool AssetManager::isModelLoadPending(const std::string& filepath) const
{
    return modelManager().isModelLoadPending(filepath);
}

bool AssetManager::isModelLoaded(const std::string& filepath) const
{
    return modelManager().isModelLoaded(filepath);
}

std::shared_ptr<Model> AssetManager::getModel(const std::string& filepath) const
{
    return modelManager().getModel(filepath);
}

ModelFuture AssetModelManager::loadModel(const AssetManager::ModelLoadRequest& request)
{
    if (request.filepath.empty()) {
        return {};
    }

    AssetManager::ModelLoadRequest normalized = request;
    normalized.filepath = AssetManager::normalizeAssetPath(normalized.filepath);

    if (isModelLoaded(normalized.filepath)) {
        if (!normalized.name.empty()) {
            _modalName2Path[normalized.name] = normalized.filepath;
        }
        if (normalized.onReady) {
            const auto model = modelCache[normalized.filepath];
            AssetManager::dispatchToGameThread([onReady = normalized.onReady, model]() mutable
                                               { onReady(model); });
        }
        return ModelFuture(modelCache[normalized.filepath]);
    }

    {
        std::lock_guard lock(_mutex);
        if (_failedModelLoads.contains(normalized.filepath)) {
            if (normalized.onReady) {
                AssetManager::dispatchToGameThread([onReady = normalized.onReady]() mutable
                                                   { onReady(nullptr); });
            }
            return {};
        }
    }

    if (!isModelLoadPending(normalized.filepath)) {
        submitModelLoad(normalized.filepath, normalized.name);
    }

    if (!normalized.name.empty()) {
        _modalName2Path[normalized.name] = normalized.filepath;
    }

    if (normalized.onReady) {
        registerModelCallback(normalized.filepath, normalized.onReady);
    }

    return {};
}

std::shared_ptr<Model> AssetModelManager::loadModelSync(const std::string& filepath)
{
    const auto normalizedFilepath = AssetManager::normalizeAssetPath(filepath);
    if (isModelLoaded(normalizedFilepath)) {
        return modelCache[normalizedFilepath];
    }

    return loadModelImpl(normalizedFilepath, "");
}

std::shared_ptr<Model> AssetModelManager::loadModelSync(const std::string& name, const std::string& filepath)
{
    const auto normalizedFilepath = AssetManager::normalizeAssetPath(filepath);
    if (isModelLoaded(normalizedFilepath)) {
        return modelCache[normalizedFilepath];
    }

    auto model = loadModelImpl(normalizedFilepath, name);
    if (model) {
        _modalName2Path[name] = normalizedFilepath;
    }
    return model;
}

void AssetModelManager::submitModelLoad(const std::string& filepath, const std::string& name)
{
    YA_CORE_INFO("submitModelLoad: async decode '{}'", filepath);

    auto handle = TaskQueue::get().submitWithCallback(
        [filepath]() -> ImportedModelData
        {
            return ImportedModelData::decode(filepath);
        },
        [this, filepath, name](ImportedModelData decoded)
        {
            std::vector<AssetManager::ModelReadyCallback> callbacks;
            std::shared_ptr<Model>                        readyModel;

            {
                std::lock_guard lock(_mutex);
                auto            existing = modelCache.find(filepath);
                if (existing != modelCache.end()) {
                    _pendingModelLoads.erase(filepath);
                    callbacks  = takeModelCallbacks(filepath);
                    readyModel = existing->second;
                }
            }

            if (readyModel) {
                dispatchModelCallbacks(callbacks, readyModel);
                return;
            }

            if (!decoded.isValid()) {
                YA_CORE_WARN("Async model decode failed for '{}'", filepath);
                {
                    std::lock_guard lock(_mutex);
                    _pendingModelLoads.erase(filepath);
                    _failedModelLoads.insert(filepath);
                    callbacks = takeModelCallbacks(filepath);
                }
                dispatchModelCallbacks(callbacks, nullptr);
                return;
            }

            auto model = decoded.createModel();
            if (!model) {
                YA_CORE_WARN("Async GPU mesh creation failed for '{}'", filepath);
                {
                    std::lock_guard lock(_mutex);
                    _pendingModelLoads.erase(filepath);
                    _failedModelLoads.insert(filepath);
                    callbacks = takeModelCallbacks(filepath);
                }
                dispatchModelCallbacks(callbacks, nullptr);
                return;
            }

            {
                std::lock_guard lock(_mutex);
                modelCache[filepath] = model;
                if (!name.empty()) {
                    _modalName2Path[name] = filepath;
                }
                _failedModelLoads.erase(filepath);
                _pendingModelLoads.erase(filepath);
                callbacks = takeModelCallbacks(filepath);
            }

            dispatchModelCallbacks(callbacks, model);

            YA_CORE_INFO("Async model ready: '{}' ({} meshes, {} materials, {} bones)",
                         filepath,
                         model->meshes.size(),
                         model->embeddedMaterials.size(),
                         model->getSkeleton() ? model->getSkeleton()->bones.size() : 0);
        });

    std::lock_guard lock(_mutex);
    _pendingModelLoads[filepath] = std::move(handle);
}

bool AssetModelManager::isModelLoadPending(const std::string& filepath) const
{
    const auto normalizedFilepath = AssetManager::normalizeAssetPath(filepath);
    std::lock_guard lock(_mutex);
    auto            it = _pendingModelLoads.find(normalizedFilepath);
    if (it == _pendingModelLoads.end()) return false;
    return !it->second.isReady();
}

bool AssetModelManager::isModelLoaded(const std::string& filepath) const
{
    const auto normalizedFilepath = AssetManager::normalizeAssetPath(filepath);
    return modelCache.find(normalizedFilepath) != modelCache.end();
}

std::shared_ptr<Model> AssetModelManager::getModel(const std::string& filepath) const
{
    const auto normalizedFilepath = AssetManager::normalizeAssetPath(filepath);
    if (isModelLoaded(normalizedFilepath)) {
        return modelCache.at(normalizedFilepath);
    }
    return nullptr;
}

std::shared_ptr<Model> AssetModelManager::loadModelImpl(const std::string& filepath, const std::string& identifier)
{
    (void)identifier;

    const auto normalizedFilepath = AssetManager::normalizeAssetPath(filepath);
    if (isModelLoaded(normalizedFilepath)) {
        return modelCache[normalizedFilepath];
    }

    auto decoded = ImportedModelData::decode(normalizedFilepath);
    if (!decoded.isValid()) {
        YA_CORE_ERROR("loadModelImpl: Failed to decode model: {}", normalizedFilepath);
        std::lock_guard lock(_mutex);
        _failedModelLoads.insert(normalizedFilepath);
        return nullptr;
    }

    auto model = decoded.createModel();
    if (!model) {
        YA_CORE_ERROR("loadModelImpl: Failed to create GPU meshes for: {}", normalizedFilepath);
        std::lock_guard lock(_mutex);
        _failedModelLoads.insert(normalizedFilepath);
        return nullptr;
    }

    modelCache[normalizedFilepath] = model;
    _failedModelLoads.erase(normalizedFilepath);
    return model;
}

size_t AssetModelManager::collectUnused(uint64_t frame)
{
    size_t          released = 0;
    auto&           ddq      = DeferredDeletionQueue::get();
    std::lock_guard lock(_mutex);
    for (auto it = modelCache.begin(); it != modelCache.end();) {
        if (it->second && it->second.use_count() == 1) {
            ddq.enqueueResource(frame, std::move(it->second));
            it = modelCache.erase(it);
            ++released;
        }
        else {
            ++it;
        }
    }
    return released;
}

bool AssetModelManager::unload(const std::string& cacheKey, uint64_t frame)
{
    const auto normalizedCacheKey = AssetManager::normalizeAssetPath(cacheKey);
    std::lock_guard lock(_mutex);
    auto            it = modelCache.find(normalizedCacheKey);
    if (it == modelCache.end()) {
        return false;
    }

    DeferredDeletionQueue::get().enqueueResource(frame, std::move(it->second));
    modelCache.erase(it);
    return true;
}

void AssetModelManager::invalidate(const std::string& filepath, uint64_t frame)
{
    evictCachedAsset(filepath, frame);
}

void AssetModelManager::evictCachedAsset(const std::string& assetPath, uint64_t frame)
{
    const auto normalizedAssetPath = AssetManager::normalizeAssetPath(assetPath);
    std::lock_guard lock(_mutex);
    auto            it = modelCache.find(normalizedAssetPath);
    if (it != modelCache.end()) {
        DeferredDeletionQueue::get().enqueueResource(frame, std::move(it->second));
        modelCache.erase(it);
    }
}

void AssetModelManager::fillStats(AssetManager::CacheStats& stats) const
{
    std::lock_guard lock(_mutex);
    stats.modelCount += modelCache.size();
}

void AssetModelManager::registerModelCallback(const std::string& filepath, AssetManager::ModelReadyCallback onReady)
{
    if (!onReady) {
        return;
    }

    std::shared_ptr<Model> readyModel;
    {
        std::lock_guard lock(_mutex);
        auto            loadedIt = modelCache.find(filepath);
        if (loadedIt != modelCache.end()) {
            readyModel = loadedIt->second;
        }
        else {
            _pendingModelCallbacks[filepath].push_back(std::move(onReady));
            return;
        }
    }

    AssetManager::dispatchToGameThread([onReady = std::move(onReady), readyModel]() mutable
                                       { onReady(readyModel); });
}

std::vector<AssetManager::ModelReadyCallback> AssetModelManager::takeModelCallbacks(const std::string& filepath)
{
    auto it = _pendingModelCallbacks.find(filepath);
    if (it == _pendingModelCallbacks.end()) {
        return {};
    }

    auto callbacks = std::move(it->second);
    _pendingModelCallbacks.erase(it);
    return callbacks;
}

void AssetModelManager::dispatchModelCallbacks(const std::vector<AssetManager::ModelReadyCallback>& callbacks,
                                               const std::shared_ptr<Model>&                        model)
{
    for (const auto& callback : callbacks) {
        if (!callback) {
            continue;
        }

        AssetManager::dispatchToGameThread([callback, model]()
                                           { callback(model); });
    }
}

} // namespace ya
