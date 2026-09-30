#pragma once

#include "Core/Common/AssetSlot.h"
#include "Core/Common/Types.h"
#include "Core/Reflection/Reflection.h"
#include "Core/System/VirtualFileSystem.h"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>



namespace ya
{

// Forward declarations
struct Texture;
struct Model;
struct Mesh;
struct Tileset;

/// Canonical asset-path form shared by AssetRef and the resource layer
/// (mount-style `Engine:` prefixes, `\` -> `/`, lexical normalization).
YA_CORE_API std::string canonicalizeAssetPath(std::string path);

/**
 * @brief Asset type enumeration
 */
enum class EAssetType : uint8_t
{
    Unknown = 0,
    Texture,
    Model,
    Mesh,
    // Extensible for future asset types
};

enum class EAssetResolveState : uint8_t
{
    Empty = 0,
    Loading,
    Ready,
    Failed,
};

struct YA_CORE_API AssetRefBase
{
    YA_REFLECT_BEGIN(AssetRefBase)
    YA_REFLECT_FIELD(_path) // Only serialize path
    YA_REFLECT_END()


  protected:
    std::string _path; // Serialized data: asset path

  public:
    AssetRefBase() = default;
    explicit AssetRefBase(const std::string &path) : _path(normalizePath(path)) {}

    /// Re-derive whatever the ref holds from its current path. Every path
    /// write goes through here (setters, deserialization), so a ref with a
    /// path is always bound to its asset.
    virtual void rebind() = 0;

    const std::string &getPath() const { return _path; }
    bool               hasPath() const { return !_path.empty(); }
    static std::string normalizePath(std::string path);

    /// Set the path and bind to its asset. In-place edits are discovered
    /// through the scene edit funnel (entt on_update), not through the ref:
    /// a ref write on its own reaches nobody.
    void setPath(const std::string &path)
    {
        _path = normalizePath(path);
        rebind();
    }
};


struct YA_CORE_API TextureRef : public AssetRefBase
{
    YA_REFLECT_BEGIN(TextureRef, AssetRefBase)
    YA_REFLECT_END()
    YA_REFLECT_COPIES_AS_VALUE()

    // Shared slot for the path; copies share it. Null when the path is empty
    // or no resource layer can load textures (reads as Failed).
    AssetHandle<Texture> _handle;

    TextureRef() = default;
    explicit TextureRef(const std::string& path) : AssetRefBase(path) { rebind(); }

    /// The loaded texture, or null unless the slot is Ready. No placeholder:
    /// each consumer decides its own fallback.
    Texture*           get() const { return isLoaded() ? _handle->resource.get() : nullptr; }
    ya::Ptr<Texture>   getShared() const { return isLoaded() ? _handle->resource : nullptr; }
    bool               isLoaded() const { return _handle && _handle->state == EAssetSlotState::Ready; }
    bool               isLoading() const { return _handle && _handle->state == EAssetSlotState::Loading; }
    EAssetResolveState getResolveState() const;
    void               rebind() override;
};

struct YA_CORE_API ModelRef : public AssetRefBase
{
    YA_REFLECT_BEGIN(ModelRef, AssetRefBase)
    YA_REFLECT_END()
    YA_REFLECT_COPIES_AS_VALUE()

    // Shared slot for the path; copies share it. Null when the path is empty
    // or no resource layer can load models (reads as Failed).
    AssetHandle<Model> _handle;

    ModelRef() = default;
    explicit ModelRef(const std::string& path) : AssetRefBase(path) { rebind(); }

    /// The loaded model, or null unless the slot is Ready.
    Model*              get() const { return isLoaded() ? _handle->resource.get() : nullptr; }
    ya::Ptr<Model>      getShared() const { return isLoaded() ? _handle->resource : nullptr; }
    bool                isLoaded() const { return _handle && _handle->state == EAssetSlotState::Ready; }
    bool                isLoading() const { return _handle && _handle->state == EAssetSlotState::Loading; }
    EAssetResolveState  getResolveState() const;
    void                rebind() override;
};

struct YA_CORE_API MeshRef : public AssetRefBase
{
    YA_REFLECT_BEGIN(MeshRef, AssetRefBase)
    YA_REFLECT_END()
    YA_REFLECT_COPIES_AS_VALUE()

    // Meshes are not standalone assets: they live inside a Model, and
    // MeshSource (Model path + mesh index) is how a mesh is referenced. The
    // handle stays null until a mesh asset type exists; a path-bearing MeshRef
    // reads as Failed.
    AssetHandle<Mesh> _handle;

    MeshRef() = default;
    explicit MeshRef(const std::string& path) : AssetRefBase(path) {}

    /// Nothing to bind: meshes are not standalone assets (see _handle).
    void rebind() override {}

    Mesh*               get() const { return isLoaded() ? _handle->resource.get() : nullptr; }
    ya::Ptr<Mesh>       getShared() const { return isLoaded() ? _handle->resource : nullptr; }
    bool                isLoaded() const { return _handle && _handle->state == EAssetSlotState::Ready; }
    bool                isLoading() const { return _handle && _handle->state == EAssetSlotState::Loading; }
    EAssetResolveState  getResolveState() const;
};

// ============================================================================
// Asset Reference Resolution Interface
// ============================================================================

/// True for the concrete asset-ref types (TextureRef / ModelRef / MeshRef /
/// TilesetRef); reflection edits and deserializes them through AssetRefBase.
YA_CORE_API bool isAssetRefType(type_index_t typeIndex);

/**
 * @brief Resource-layer hooks for the Core asset-ref types. The resource
 *        layer installs one at static-init time; pure GUI hosts have none and
 *        their refs bind to nothing (read as Failed).
 */
struct IAssetRefResolver
{
    virtual ~IAssetRefResolver() = default;

    /// Shared texture slot for a normalized, non-empty path. Null when no
    /// texture can be produced (no render backend).
    virtual AssetHandle<Texture> acquireTexture(const std::string &path) const = 0;

    /// Shared model slot for a normalized, non-empty path. Null when no
    /// model can be produced.
    virtual AssetHandle<Model> acquireModel(const std::string &path) const = 0;

    /// Shared tileset slot for a normalized, non-empty path; tilesets are
    /// parsed synchronously on first request. Null when no tileset can be
    /// produced.
    virtual AssetHandle<Tileset> acquireTileset(const std::string &path) const = 0;
};

/// Currently installed asset-ref resolver (null in pure-GUI hosts). The
/// resource layer installs its engine implementation at static-init time.
YA_CORE_API const IAssetRefResolver *getAssetRefResolver();
YA_CORE_API void                    setAssetRefResolver(const IAssetRefResolver *resolver);

} // namespace ya
