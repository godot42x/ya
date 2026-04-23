#pragma once

#include <mutex>
#include <unordered_map>

#include "Core/Async/TaskQueue.h"
#include "Resource/AssetManager.h"

namespace ya
{

class AssetModelManager
{
  private:
    AssetManager& _owner;

    std::unordered_map<std::string, std::shared_ptr<Model>> modelCache;
    std::unordered_map<std::string, std::string>            _modalName2Path;
    std::unordered_map<std::string, TaskHandle<ImportedModelData>> _pendingModelLoads;
    std::unordered_map<std::string, std::vector<AssetManager::ModelReadyCallback>> _pendingModelCallbacks;
    mutable std::mutex _mutex;

  public:
    explicit AssetModelManager(AssetManager& owner);

    void clear();

    ModelFuture loadModel(const AssetManager::ModelLoadRequest& request);
    std::shared_ptr<Model> loadModelSync(const std::string& filepath);
    std::shared_ptr<Model> loadModelSync(const std::string& name, const std::string& filepath);
    std::shared_ptr<Model> getModel(const std::string& filepath) const;
    bool isModelLoaded(const std::string& filepath) const;
    bool isModelLoadPending(const std::string& filepath) const;

    size_t collectUnused(uint64_t frame);
    bool unload(const std::string& cacheKey, uint64_t frame);
    void invalidate(const std::string& filepath, uint64_t frame);
    void evictCachedAsset(const std::string& assetPath, uint64_t frame);
    void fillStats(AssetManager::CacheStats& stats) const;

  private:
    void submitModelLoad(const std::string& filepath, const std::string& name = "");
    std::shared_ptr<Model> loadModelImpl(const std::string& filepath, const std::string& identifier);
    void registerModelCallback(const std::string& filepath, AssetManager::ModelReadyCallback onReady);
    std::vector<AssetManager::ModelReadyCallback> takeModelCallbacks(const std::string& filepath);
    void dispatchModelCallbacks(const std::vector<AssetManager::ModelReadyCallback>& callbacks,
                                const std::shared_ptr<Model>&                         model);
};

} // namespace ya