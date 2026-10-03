#include "Core/Common/AssetTypeRegistry.h"

#include "Core/Log.h"

#include <mutex>

namespace ya
{

namespace
{

struct FStoreSnapshot
{
    IAssetStore*             store = nullptr;
    std::vector<std::string> extensions;
};

std::vector<FStoreSnapshot> snapshotStores(const std::vector<AssetTypeDesc>& entries)
{
    std::vector<FStoreSnapshot> stores;
    stores.reserve(entries.size());
    for (const AssetTypeDesc& desc : entries) {
        if (desc.store == nullptr) {
            continue;
        }
        stores.push_back(FStoreSnapshot{desc.store, desc.extensions});
    }
    return stores;
}

std::vector<IAssetStore*> storesForPath(const std::vector<FStoreSnapshot>& stores, const std::string& path)
{
    size_t best = 0;
    for (const FStoreSnapshot& store : stores) {
        for (const std::string& extension : store.extensions) {
            if (assetPathHasExtension(path, extension) && extension.size() > best) {
                best = extension.size();
            }
        }
    }

    std::vector<IAssetStore*> chosen;
    if (best == 0) {
        // No document extension: a registerAsset name. Every store looks the
        // key up and ignores the ones it does not hold.
        chosen.reserve(stores.size());
        for (const FStoreSnapshot& store : stores) {
            chosen.push_back(store.store);
        }
        return chosen;
    }

    for (const FStoreSnapshot& store : stores) {
        for (const std::string& extension : store.extensions) {
            if (extension.size() == best && assetPathHasExtension(path, extension)) {
                chosen.push_back(store.store);
                break;
            }
        }
    }
    return chosen;
}

} // namespace

bool AssetTypeDesc::ownsPath(const std::string& path) const
{
    for (const std::string& extension : extensions) {
        if (assetPathHasExtension(path, extension)) {
            return true;
        }
    }
    return false;
}

AssetTypeRegistry& AssetTypeRegistry::get()
{
    static AssetTypeRegistry registry;
    return registry;
}

void AssetTypeRegistry::registerType(AssetTypeDesc desc)
{
    std::lock_guard lock(_mutex);
    for (const AssetTypeDesc& existing : _entries) {
        if (existing.refType == desc.refType) {
            YA_CORE_WARN("AssetTypeRegistry: ref type of '{}' is already registered", desc.name);
            return;
        }
    }
    _entries.push_back(std::move(desc));
}

std::optional<AssetTypeDesc> AssetTypeRegistry::findByRefType(type_index_t type) const
{
    std::lock_guard lock(_mutex);
    for (const AssetTypeDesc& desc : _entries) {
        if (desc.refType == type) {
            return desc;
        }
    }
    return std::nullopt;
}

std::optional<AssetTypeDesc> AssetTypeRegistry::findByResourceType(type_index_t type) const
{
    std::lock_guard lock(_mutex);
    for (const AssetTypeDesc& desc : _entries) {
        if (desc.resourceType == type) {
            return desc;
        }
    }
    return std::nullopt;
}

std::optional<AssetTypeDesc> AssetTypeRegistry::findByPath(const std::string& path) const
{
    std::lock_guard lock(_mutex);
    const AssetTypeDesc* best     = nullptr;
    size_t               bestSize = 0;
    for (const AssetTypeDesc& desc : _entries) {
        for (const std::string& extension : desc.extensions) {
            if (assetPathHasExtension(path, extension) && extension.size() > bestSize) {
                best     = &desc;
                bestSize = extension.size();
            }
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return *best;
}

std::vector<IAssetStore*> AssetTypeRegistry::stores() const
{
    std::lock_guard lock(_mutex);
    std::vector<IAssetStore*> out;
    out.reserve(_entries.size());
    for (const AssetTypeDesc& desc : _entries) {
        if (desc.store != nullptr) {
            out.push_back(desc.store);
        }
    }
    return out;
}

bool AssetTypeRegistry::unloadPath(const std::string& path)
{
    std::vector<FStoreSnapshot> snapshot;
    {
        std::lock_guard lock(_mutex);
        snapshot = snapshotStores(_entries);
    }
    bool any = false;
    for (IAssetStore* store : storesForPath(snapshot, path)) {
        any = store->unload(path) || any;
    }
    return any;
}

void AssetTypeRegistry::invalidatePath(const std::string& path)
{
    std::vector<FStoreSnapshot> snapshot;
    {
        std::lock_guard lock(_mutex);
        snapshot = snapshotStores(_entries);
    }
    for (IAssetStore* store : storesForPath(snapshot, path)) {
        store->invalidate(path);
    }
}

} // namespace ya
