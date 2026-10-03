#pragma once

#include "Core/Api.h"
#include "Core/Common/AssetSlot.h"
#include "Core/TypeIndex.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

// Type-erased document (or other) slot table. Texture / Model / Mesh register
// a description only; their store pointer stays null.
class IAssetStore
{
  public:
    virtual ~IAssetStore() = default;

    // Slot handles are shared_ptr<const AssetSlot<T>>; const void keeps that const.
    virtual std::shared_ptr<const void> acquireErased(const std::string& path) = 0;
    virtual bool                  isLoaded(const std::string& path) const = 0;
    virtual bool                  unload(const std::string& path)         = 0;
    virtual void                  invalidate(const std::string& path)     = 0;
    virtual size_t                collectUnused()                          = 0;
    virtual void                  clear()                                  = 0;
    virtual bool                  ownsPath(const std::string& path) const  = 0;
};

template <typename T>
class IDocumentAssetStore : public IAssetStore
{
  public:
    virtual AssetHandle<T>      acquire(const std::string& path)                                      = 0;
    virtual void                registerAsset(const std::string& name, const std::shared_ptr<T>& asset) = 0;
    virtual std::shared_ptr<T>  get(const std::string& path) const                                    = 0;

    std::shared_ptr<const void> acquireErased(const std::string& path) override { return acquire(path); }
};

[[nodiscard]] inline bool assetPathHasExtension(const std::string& path, const std::string& extension)
{
    return !extension.empty() && path.size() >= extension.size() &&
           path.compare(path.size() - extension.size(), extension.size(), extension) == 0;
}

struct YA_CORE_API AssetTypeDesc
{
    std::string              name;
    std::string              displayName;
    std::vector<std::string> extensions;
    type_index_t             refType       = 0;
    type_index_t             resourceType  = 0;
    IAssetStore*             store         = nullptr;

    [[nodiscard]] bool ownsPath(const std::string& path) const;
};

// Process-wide list of asset types. Registration happens at static init and
// is mutex-guarded. Adding a document asset is one registerDocument call in
// that asset's cpp; the engine files do not grow a per-type branch.
class YA_CORE_API AssetTypeRegistry
{
  private:
    mutable std::mutex       _mutex;
    std::vector<AssetTypeDesc> _entries;

  public:
    static AssetTypeRegistry& get();

    void registerType(AssetTypeDesc desc);

    /// Document asset: builds one store for T and records RefT as its ref type.
    /// Defined in AssetDocumentManager.h (the store implementation).
    template <typename T, typename Traits, typename RefT>
    static void registerDocument(std::string name, std::string displayName, std::vector<std::string> extensions);

    [[nodiscard]] std::optional<AssetTypeDesc> findByRefType(type_index_t type) const;
    [[nodiscard]] std::optional<AssetTypeDesc> findByResourceType(type_index_t type) const;
    [[nodiscard]] std::optional<AssetTypeDesc> findByPath(const std::string& path) const;
    [[nodiscard]] std::vector<IAssetStore*>    stores() const;

    template <typename T>
    [[nodiscard]] IDocumentAssetStore<T>* store() const
    {
        const std::optional<AssetTypeDesc> desc = findByResourceType(ya::type_index_v<T>);
        if (!desc || desc->store == nullptr) {
            return nullptr;
        }
        return static_cast<IDocumentAssetStore<T>*>(desc->store);
    }

    /// Unload every store that owns this path. A path with no registered
    /// extension is offered to every store, so a name installed with
    /// registerAsset (no file suffix) still unloads.
    bool unloadPath(const std::string& path);
    void invalidatePath(const std::string& path);
};

} // namespace ya
