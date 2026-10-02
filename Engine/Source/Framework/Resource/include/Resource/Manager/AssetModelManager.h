#pragma once

#include "Core/Base.h"

#include <atomic>
#include <functional>
#include <mutex>

#include "Core/Async/TaskQueue.h"
#include "Core/Common/AssetSlot.h"
#include "Resource/AssetManager.h"

namespace ya
{

class AssetModelManager
{
  private:
    // What one slot update owes its subscribers: ready callbacks and update
    // observers, gathered under the manager lock and dispatched outside it.
    struct SlotUpdate
    {
        std::vector<AssetManager::ModelReadyCallback> readyCallbacks;
        std::vector<std::function<void()>>            updateObservers;
    };

    // One shared slot per source path. The entry keeps what a reload needs;
    // the slot is what refs hold.
    struct ModelEntry
    {
        std::shared_ptr<AssetSlot<Model>> slot;
        std::string                       filepath; // normalized source path
        // Serial of the decode whose completion may fill the slot; bumped by
        // every (re)load so an older in-flight decode is dropped.
        uint64_t                                       loadSerial = 0;
        std::vector<AssetManager::ModelReadyCallback> readyCallbacks;
    };

    AssetManager& _owner;

    std::unordered_map<std::string, ModelEntry> _entries;
    std::unordered_map<std::string, std::string> _modelName2Path;
    uint64_t                                    _clearGeneration = 0;
    uint64_t                                    _nextLoadSerial  = 0;
    // Slots currently in Loading. Maintained on the slot-state transitions below.
    std::atomic<uint32_t>                       _loadingSlotCount{0};
    mutable std::mutex                          _mutex;

  public:
    explicit AssetModelManager(AssetManager& owner);

    void clear();

    AssetHandle<Model>    loadModel(const AssetManager::ModelLoadRequest& request);
    std::shared_ptr<Model> loadModelSync(const std::string& filepath);
    std::shared_ptr<Model> loadModelSync(const std::string& name, const std::string& filepath);
    std::shared_ptr<Model> getModel(const std::string& filepath) const;
    bool                    isModelLoaded(const std::string& filepath) const;

    size_t collectUnused(uint64_t frame);
    bool   unload(const std::string& filepath, uint64_t frame);
    void   invalidate(const std::string& filepath, uint64_t frame);
    void   evictCachedAsset(const std::string& assetPath, uint64_t frame);
    void   fillStats(AssetManager::CacheStats& stats) const;
    /// How many model slots this manager still has in Loading.
    uint32_t loadingSlotCount() const { return _loadingSlotCount.load(std::memory_order_relaxed); }

  private:
    std::shared_ptr<Model> loadModelImpl(const std::string& filepath, const std::string& name);

    void submitModelLoad(const std::string& filepath);
    void completeModelLoad(const std::string& filepath, uint64_t serial, ImportedModelData decoded);

    // A new entry slot starts Loading and is counted. Caller holds _mutex.
    std::shared_ptr<AssetSlot<Model>> makeLoadingSlotLocked();
    // Keep _loadingSlotCount aligned with a state change. Caller holds _mutex.
    void accountSlotStateLocked(EAssetSlotState previous, EAssetSlotState next);
    // Update the slot and hand the subscribers back for dispatch outside
    // the lock. A null model marks the slot Failed. Caller holds _mutex.
    SlotUpdate updateSlotLocked(ModelEntry& entry, const std::shared_ptr<Model>& model);
    static void dispatchSlotUpdate(SlotUpdate update, const std::shared_ptr<Model>& model);
};

} // namespace ya
