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
    Dirty,
    Loading,
    Ready,
    Failed,
};

enum class EAssetResolveResult : uint8_t
{
    Pending = 0,
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

    ya::Ptr<Model> _cachedPtr;
    EAssetResolveState _resolveState    = EAssetResolveState::Empty;
    uint64_t           _resolvedVersion = 0;

    ModelRef() = default;
    explicit ModelRef(const std::string& path) : AssetRefBase(path) {}
    ModelRef(const std::string& path, ya::Ptr<Model> ptr)
        : AssetRefBase(path), _cachedPtr(std::move(ptr))
    {
        _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
    }

    ModelRef(const ModelRef& other)
        : AssetRefBase(other), _cachedPtr(other._cachedPtr), _resolveState(other._resolveState), _resolvedVersion(other._resolvedVersion)
    {}

    ModelRef& operator=(const ModelRef& other)
    {
        if (this != &other) {
            AssetRefBase::operator=(other);
            _cachedPtr       = other._cachedPtr;
            _resolveState    = other._resolveState;
            _resolvedVersion = other._resolvedVersion;
        }
        return *this;
    }

    ModelRef(ModelRef&& other) noexcept            = default;
    ModelRef& operator=(ModelRef&& other) noexcept = default;

    Model* get() const { return _cachedPtr.get(); }
    ya::Ptr<Model> getShared() const { return _cachedPtr; }
    bool isLoaded() const { return _resolveState == EAssetResolveState::Ready && _cachedPtr != nullptr; }
    bool isLoading() const { return _resolveState == EAssetResolveState::Loading; }
    EAssetResolveState getResolveState() const { return _resolveState; }
    EAssetResolveResult resolve();
    void invalidate();
    void rebind() override { invalidate(); }
    void set(const std::string& path, ya::Ptr<Model> ptr);
};

struct YA_CORE_API MeshRef : public AssetRefBase
{
    YA_REFLECT_BEGIN(MeshRef, AssetRefBase)
    YA_REFLECT_END()

    ya::Ptr<Mesh> _cachedPtr;
    EAssetResolveState _resolveState    = EAssetResolveState::Empty;
    uint64_t           _resolvedVersion = 0;

    MeshRef() = default;
    explicit MeshRef(const std::string& path) : AssetRefBase(path) {}
    MeshRef(const std::string& path, ya::Ptr<Mesh> ptr)
        : AssetRefBase(path), _cachedPtr(std::move(ptr))
    {
        _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
    }

    MeshRef(const MeshRef& other)
        : AssetRefBase(other), _cachedPtr(other._cachedPtr), _resolveState(other._resolveState), _resolvedVersion(other._resolvedVersion)
    {}

    MeshRef& operator=(const MeshRef& other)
    {
        if (this != &other) {
            AssetRefBase::operator=(other);
            _cachedPtr       = other._cachedPtr;
            _resolveState    = other._resolveState;
            _resolvedVersion = other._resolvedVersion;
        }
        return *this;
    }

    MeshRef(MeshRef&& other) noexcept            = default;
    MeshRef& operator=(MeshRef&& other) noexcept = default;

    Mesh* get() const { return _cachedPtr.get(); }
    ya::Ptr<Mesh> getShared() const { return _cachedPtr; }
    bool isLoaded() const { return _resolveState == EAssetResolveState::Ready && _cachedPtr != nullptr; }
    bool isLoading() const { return _resolveState == EAssetResolveState::Loading; }
    EAssetResolveState getResolveState() const { return _resolveState; }
    EAssetResolveResult resolve();
    void invalidate();
    void rebind() override { invalidate(); }
    void set(const std::string& path, ya::Ptr<Mesh> ptr);
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

    /// Polling resolve for ModelRef / MeshRef, which still cache their
    /// resource per ref instead of sharing a slot.
    virtual void resolveAssetRef(type_index_t typeIndex, void *assetRefPtr) const = 0;
};

/// Currently installed asset-ref resolver (null in pure-GUI hosts). The
/// resource layer installs its engine implementation at static-init time.
YA_CORE_API const IAssetRefResolver *getAssetRefResolver();
YA_CORE_API void                    setAssetRefResolver(const IAssetRefResolver *resolver);

} // namespace ya
