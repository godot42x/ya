#include "Resource/Manager/AssetModelManager.h"

#include "Core/Log.h"
#include "Resource/Core/Model/ImportedModelData.h"

#include "Core/Common/DeferredDeletionQueue.h"

namespace ya
{

AssetModelManager::AssetModelManager(AssetManager& owner)
    : _owner(owner)
{
}

void AssetModelManager::clear()
{
    std::vector<std::function<void()>> updateObservers;
    {
        std::lock_guard lock(_mutex);
        ++_clearGeneration;
        // Refs outlive the backend teardown that calls this; leave their
        // slots Failed instead of pointing at destroyed GPU objects.
        for (auto& [filepath, entry] : _entries) {
            (void)filepath;
            entry.slot->resource.reset();
            entry.slot->state = EAssetSlotState::Failed;
            ++entry.slot->generation;
            auto observers = entry.slot->observers.gather();
            updateObservers.insert(updateObservers.end(),
                                    std::make_move_iterator(observers.begin()),
                                    std::make_move_iterator(observers.end()));
        }
        _entries.clear();
        _modelName2Path.clear();
    }
    // Slots went Failed with a generation bump; subscribers hear the
    // update outside the lock. Teardown-path callbacks only enqueue or drop state.
    for (auto& observer : updateObservers) {
        observer();
    }
}

AssetHandle<Model> AssetManager::loadModel(const ModelLoadRequest& request)
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

bool AssetManager::isModelLoaded(const std::string& filepath) const
{
    return modelManager().isModelLoaded(filepath);
}

std::shared_ptr<Model> AssetManager::getModel(const std::string& filepath) const
{
    return modelManager().getModel(filepath);
}

AssetHandle<Model> AssetModelManager::loadModel(const AssetManager::ModelLoadRequest& request)
{
    if (request.filepath.empty()) {
        return nullptr;
    }

    const std::string path = AssetManager::normalizeAssetPath(request.filepath);

    AssetHandle<Model>       handle;
    std::shared_ptr<Model>   settledModel;
    bool                     bSettled  = false;
    bool                     bInserted = false;
    {
        std::lock_guard lock(_mutex);
        if (!request.name.empty()) {
            _modelName2Path[request.name] = path;
        }
        auto [it, inserted] = _entries.try_emplace(path);
        ModelEntry& entry   = it->second;
        bInserted           = inserted;
        if (inserted) {
            entry.slot     = std::make_shared<AssetSlot<Model>>();
            entry.filepath = path;
        }
        handle = entry.slot;
        if (entry.slot->state == EAssetSlotState::Loading) {
            if (request.onReady) {
                entry.readyCallbacks.push_back(request.onReady);
            }
        }
        else {
            bSettled     = true;
            settledModel = entry.slot->resource;
        }
    }

    if (bInserted) {
        submitModelLoad(path);
    }
    if (bSettled && request.onReady) {
        AssetManager::dispatchToGameThread([onReady = request.onReady, settledModel]() mutable
                                            { onReady(settledModel); });
    }
    return handle;
}

std::shared_ptr<Model> AssetModelManager::loadModelSync(const std::string& filepath)
{
    return loadModelImpl(filepath, "");
}

std::shared_ptr<Model> AssetModelManager::loadModelSync(const std::string& name, const std::string& filepath)
{
    auto model = loadModelImpl(filepath, name);
    if (model) {
        std::lock_guard lock(_mutex);
        _modelName2Path[name] = AssetManager::normalizeAssetPath(filepath);
    }
    return model;
}

std::shared_ptr<Model> AssetModelManager::getModel(const std::string& filepath) const
{
    const auto path = AssetManager::normalizeAssetPath(filepath);
    std::lock_guard lock(_mutex);
    auto            it = _entries.find(path);
    return it != _entries.end() && it->second.slot->state == EAssetSlotState::Ready
               ? it->second.slot->resource
               : nullptr;
}

bool AssetModelManager::isModelLoaded(const std::string& filepath) const
{
    return getModel(filepath) != nullptr;
}

std::shared_ptr<Model> AssetModelManager::loadModelImpl(const std::string& filepath, const std::string& name)
{
    (void)name;

    const auto path = AssetManager::normalizeAssetPath(filepath);

    {
        std::lock_guard lock(_mutex);
        if (auto it = _entries.find(path); it != _entries.end() && it->second.slot->state == EAssetSlotState::Ready) {
            return it->second.slot->resource;
        }
    }

    auto decoded = ImportedModelData::decode(path);
    if (!decoded.isValid()) {
        YA_CORE_ERROR("loadModelImpl: Failed to decode model: {}", path);
        SlotUpdate update;
        {
            std::lock_guard lock(_mutex);
            auto [it, inserted] = _entries.try_emplace(path);
            ModelEntry& entry   = it->second;
            if (inserted) {
                entry.slot     = std::make_shared<AssetSlot<Model>>();
                entry.filepath = path;
            }
            // Supersedes any async decode still in flight for this slot.
            entry.loadSerial = ++_nextLoadSerial;
            update           = updateSlotLocked(entry, nullptr);
        }
        dispatchSlotUpdate(std::move(update), nullptr);
        return nullptr;
    }

    auto* render = _owner.getRender();
    if (!render) {
        YA_CORE_ERROR("loadModelImpl: Render backend unavailable for GPU mesh creation: {}", path);
        SlotUpdate update;
        {
            std::lock_guard lock(_mutex);
            auto [it, inserted] = _entries.try_emplace(path);
            ModelEntry& entry   = it->second;
            if (inserted) {
                entry.slot     = std::make_shared<AssetSlot<Model>>();
                entry.filepath = path;
            }
            entry.loadSerial = ++_nextLoadSerial;
            update           = updateSlotLocked(entry, nullptr);
        }
        dispatchSlotUpdate(std::move(update), nullptr);
        return nullptr;
    }

    auto model = decoded.createModel(*render);
    if (!model) {
        YA_CORE_ERROR("loadModelImpl: Failed to create GPU meshes for: {}", path);
        SlotUpdate update;
        {
            std::lock_guard lock(_mutex);
            auto [it, inserted] = _entries.try_emplace(path);
            ModelEntry& entry   = it->second;
            if (inserted) {
                entry.slot     = std::make_shared<AssetSlot<Model>>();
                entry.filepath = path;
            }
            entry.loadSerial = ++_nextLoadSerial;
            update           = updateSlotLocked(entry, nullptr);
        }
        dispatchSlotUpdate(std::move(update), nullptr);
        return nullptr;
    }

    SlotUpdate update;
    {
        std::lock_guard lock(_mutex);
        auto [it, inserted] = _entries.try_emplace(path);
        ModelEntry& entry   = it->second;
        if (inserted) {
            entry.slot     = std::make_shared<AssetSlot<Model>>();
            entry.filepath = path;
        }
        // Supersedes any async decode still in flight for this slot.
        entry.loadSerial = ++_nextLoadSerial;
        update           = updateSlotLocked(entry, model);
    }
    dispatchSlotUpdate(std::move(update), model);
    return model;
}

void AssetModelManager::submitModelLoad(const std::string& filepath)
{
    YA_CORE_INFO("submitModelLoad: async decode '{}'", filepath);

    uint64_t serial = 0;
    {
        std::lock_guard lock(_mutex);
        auto            it = _entries.find(filepath);
        if (it == _entries.end()) {
            return;
        }
        serial                = ++_nextLoadSerial;
        it->second.loadSerial = serial;
    }

    TaskQueue::get().submitWithCallback(
        [filepath]() -> ImportedModelData
        {
            return ImportedModelData::decode(filepath);
        },
        [this, filepath, serial](ImportedModelData decoded)
        {
            completeModelLoad(filepath, serial, std::move(decoded));
        });
}

void AssetModelManager::completeModelLoad(const std::string& filepath, uint64_t serial, ImportedModelData decoded)
{
    std::string label;
    {
        std::lock_guard lock(_mutex);
        auto            it = _entries.find(filepath);
        if (it == _entries.end() || it->second.loadSerial != serial) {
            return;
        }
        label = it->second.filepath;
    }

    std::shared_ptr<Model> model;
    if (decoded.isValid()) {
        auto* render = _owner.getRender();
        if (render) {
            model = decoded.createModel(*render);
        }
    }
    if (!model) {
        YA_CORE_WARN("Async model decode failed for '{}'", filepath);
    }

    SlotUpdate update;
    {
        std::lock_guard lock(_mutex);
        auto            it = _entries.find(filepath);
        if (it == _entries.end() || it->second.loadSerial != serial) {
            return;
        }
        update = updateSlotLocked(it->second, model);
    }
    dispatchSlotUpdate(std::move(update), model);

    if (model) {
        YA_CORE_INFO("Async model ready: '{}' ({} meshes, {} materials, {} bones)",
                     label,
                     model->meshes.size(),
                     model->embeddedMaterials.size(),
                     model->getSkeleton() ? model->getSkeleton()->bones.size() : 0);
    }
}

size_t AssetModelManager::collectUnused(uint64_t frame)
{
    size_t          released = 0;
    auto&           ddq      = DeferredDeletionQueue::get();
    std::lock_guard lock(_mutex);
    for (auto it = _entries.begin(); it != _entries.end();) {
        const auto& slot = it->second.slot;
        // Only the manager holds the slot, and nothing outside the slot still
        // uses the model (e.g. a mesh source that kept its handle).
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
        std::erase_if(_modelName2Path, [&](const auto& alias) { return alias.second == it->first; });
        it = _entries.erase(it);
        ++released;
    }
    return released;
}

bool AssetModelManager::unload(const std::string& filepath, uint64_t frame)
{
    const auto path = AssetManager::normalizeAssetPath(filepath);

    SlotUpdate update;
    {
        std::lock_guard lock(_mutex);
        auto&           ddq = DeferredDeletionQueue::get();
        auto            it  = _entries.find(path);
        if (it == _entries.end()) {
            return false;
        }
        // Refs still holding the slot see it Failed; they load again only
        // when their path is rebound.
        AssetSlot<Model>& slot = *it->second.slot;
        if (slot.resource) {
            ddq.enqueueResource(frame, std::move(slot.resource));
        }
        update = updateSlotLocked(it->second, nullptr);
        std::erase_if(_modelName2Path, [&](const auto& alias) { return alias.second == path; });
        _entries.erase(it);
    }
    dispatchSlotUpdate(std::move(update), nullptr);
    return true;
}

void AssetModelManager::invalidate(const std::string& filepath, uint64_t frame)
{
    evictCachedAsset(filepath, frame);
}

void AssetModelManager::evictCachedAsset(const std::string& assetPath, uint64_t frame)
{
    const auto path = AssetManager::normalizeAssetPath(assetPath);

    SlotUpdate update;
    {
        std::lock_guard lock(_mutex);
        auto&           ddq = DeferredDeletionQueue::get();
        auto            it  = _entries.find(path);
        if (it == _entries.end()) {
            return;
        }
        AssetSlot<Model>& slot = *it->second.slot;
        if (slot.resource) {
            ddq.enqueueResource(frame, std::move(slot.resource));
        }
        update = updateSlotLocked(it->second, nullptr);
        _entries.erase(it);
    }
    dispatchSlotUpdate(std::move(update), nullptr);
}

void AssetModelManager::fillStats(AssetManager::CacheStats& stats) const
{
    std::lock_guard lock(_mutex);
    for (const auto& [filepath, entry] : _entries) {
        (void)filepath;
        if (entry.slot->state == EAssetSlotState::Ready) {
            ++stats.modelCount;
        }
    }
}

AssetModelManager::SlotUpdate AssetModelManager::updateSlotLocked(ModelEntry&                   entry,
                                                                 const std::shared_ptr<Model>& model)
{
    AssetSlot<Model>& slot = *entry.slot;
    if (slot.resource && slot.resource != model) {
        DeferredDeletionQueue::get().retire(std::move(slot.resource));
    }
    slot.resource = model;
    slot.state    = model ? EAssetSlotState::Ready : EAssetSlotState::Failed;
    ++slot.generation;
    SlotUpdate update;
    update.readyCallbacks  = std::exchange(entry.readyCallbacks, {});
    update.updateObservers = slot.observers.gather();
    return update;
}

void AssetModelManager::dispatchSlotUpdate(SlotUpdate update, const std::shared_ptr<Model>& model)
{
    for (auto& callback : update.readyCallbacks) {
        if (!callback) {
            continue;
        }
        AssetManager::dispatchToGameThread([callback = std::move(callback), model]()
                                           { callback(model); });
    }
    // Updates land on the game thread (async completions hop through the
    // TaskQueue main-thread drain); observer callbacks only enqueue work.
    for (auto& observer : update.updateObservers) {
        observer();
    }
}

} // namespace ya
