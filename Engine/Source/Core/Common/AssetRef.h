#pragma once

#include "Core/Common/Types.h"
#include "Core/Delegate.h"
#include "Core/Reflection/Reflection.h"
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

struct AssetRefBase
{
    YA_REFLECT_BEGIN(AssetRefBase)
    YA_REFLECT_FIELD(_path) // Only serialize path
    YA_REFLECT_END()


  protected:
    std::string _path; // Serialized data: asset path

  public:
    MulticastDelegate<void()> onModified;
    // bool bValid= false;  // cannot be loaded?



    AssetRefBase() = default;
    explicit AssetRefBase(const std::string &path) : _path(normalizePath(path)) {}

    virtual EAssetResolveResult resolve() = 0;
    // {
    // resolve path from abs to engine?
    // }
    virtual void invalidate() = 0;

    const std::string &getPath() const { return _path; }
    bool               hasPath() const { return !_path.empty(); }
    static std::string normalizePath(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    void               setPath(const std::string &path)
    {
        _path = normalizePath(path);
        invalidate();
        notifyModified();
    }

    /**
     * @brief Set path without triggering modification callback
     * Used when initializing from external source (e.g., shared material)
     * where we don't want to trigger a resolve cycle
     */
    void setPathWithoutNotify(const std::string &path)
    {
        _path = normalizePath(path);
        // Don't invalidate or notify - caller manages state
    }

    /**
     * @brief Notify that the asset was modified (enqueues to deferred queue)
     * Called by editor UI after async file picker completes.
     * The modification will be collected by RenderContext::beginInstance() on next frame.
     */
    void notifyModified()
    {
        onModified.broadcast();
    }
};


struct TextureRef : public AssetRefBase
{
    YA_REFLECT_BEGIN(TextureRef, AssetRefBase)
    YA_REFLECT_END()

    ya::Ptr<Texture> _cachedPtr;
    EAssetResolveState _resolveState    = EAssetResolveState::Empty;
    uint64_t           _resolvedVersion = 0;

    TextureRef() = default;
    explicit TextureRef(const std::string& path) : AssetRefBase(path) {}
    TextureRef(const std::string& path, ya::Ptr<Texture> ptr)
        : AssetRefBase(path), _cachedPtr(std::move(ptr))
    {
        _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
    }

    TextureRef(const TextureRef& other)
        : AssetRefBase(other), _cachedPtr(other._cachedPtr), _resolveState(other._resolveState), _resolvedVersion(other._resolvedVersion)
    {}

    TextureRef& operator=(const TextureRef& other)
    {
        if (this != &other) {
            AssetRefBase::operator=(other);
            _cachedPtr       = other._cachedPtr;
            _resolveState    = other._resolveState;
            _resolvedVersion = other._resolvedVersion;
        }
        return *this;
    }

    TextureRef(TextureRef&& other) noexcept            = default;
    TextureRef& operator=(TextureRef&& other) noexcept = default;

    Texture* get() const { return _cachedPtr.get(); }
    ya::Ptr<Texture> getShared() const { return _cachedPtr; }
    bool isLoaded() const { return _resolveState == EAssetResolveState::Ready && _cachedPtr != nullptr; }
    bool isLoading() const { return _resolveState == EAssetResolveState::Loading; }
    EAssetResolveState getResolveState() const { return _resolveState; }
    bool isStale() const;
    EAssetResolveResult resolve() override;
    void invalidate() override;
    void set(const std::string& path, ya::Ptr<Texture> ptr);
};

struct ModelRef : public AssetRefBase
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
    bool isStale() const;
    EAssetResolveResult resolve() override;
    void invalidate() override;
    void set(const std::string& path, ya::Ptr<Model> ptr);
};

struct MeshRef : public AssetRefBase
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
    bool isStale() const;
    EAssetResolveResult resolve() override;
    void invalidate() override;
    void set(const std::string& path, ya::Ptr<Mesh> ptr);
};

// ============================================================================
// Asset Reference Resolution Interface
// ============================================================================

/**
 * @brief Interface for resolving asset references
 * Used by ReflectionSerializer to resolve asset refs after deserialization
 */
struct IAssetRefResolver
{
    virtual ~IAssetRefResolver() = default;

    /**
     * @brief Check if a type index represents an asset reference type
     */
    virtual bool isAssetRefType(uint32_t typeIndex) const = 0;

    /**
     * @brief Resolve an asset reference (load the asset from path)
     * @param typeIndex Type index of the concrete asset ref
     * @param assetRefPtr Pointer to the asset ref instance
     */
    virtual void resolveAssetRef(uint32_t typeIndex, void *assetRefPtr) const = 0;
};

/**
 * @brief Default asset reference resolver implementation
 */
struct DefaultAssetRefResolver : public IAssetRefResolver
{
    static DefaultAssetRefResolver &instance();

    bool isAssetRefType(uint32_t typeIndex) const override;
    void resolveAssetRef(uint32_t typeIndex, void *assetRefPtr) const override;
};

} // namespace ya

// ============================================================================
// Asset reference resolve implementations
// Include necessary headers for inline implementations
// ============================================================================
#include "Resource/AssetManager.h"
#include "Resource/Texture/TextureLibrary.h"
#include "Core/Log.h"

namespace ya
{

inline EAssetResolveResult TextureRef::resolve()
{
    _path = AssetManager::normalizeAssetPath(_path);
    if (getPath().empty()) {
        _resolveState = EAssetResolveState::Empty;
        return EAssetResolveResult::Failed;
    }

    const auto currentVersion = AssetManager::get()->getResourceVersion(_path);

    // Ready: check version to detect reloaded resources
    if (_resolveState == EAssetResolveState::Ready && _cachedPtr) {
        if (_resolvedVersion == currentVersion) {
            return EAssetResolveResult::Ready;  // Up-to-date, fast path
        }
        // Version changed → stale pointer, force re-resolve
        _cachedPtr.reset();
        _resolveState = EAssetResolveState::Dirty;
        YA_CORE_TRACE("TextureRef: version changed for '{}', re-resolving", _path);
    }

    if (_resolveState == EAssetResolveState::Failed && _resolvedVersion == currentVersion) {
        return EAssetResolveResult::Failed;
    }

    // Loading or Dirty: try to get the real texture from cache
    auto future = AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
        .filepath = _path,
    });
    if (future.isReady()) {
        _cachedPtr        = future.getShared();
        _resolveState     = EAssetResolveState::Ready;
        _resolvedVersion  = currentVersion;
        return EAssetResolveResult::Ready;
    }

    if (AssetManager::get()->isTextureLoadFailed(_path)) {
        _resolveState    = EAssetResolveState::Failed;
        _resolvedVersion = currentVersion;
        auto placeholder = TextureLibrary::get().getCheckerboardTexture();
        if (placeholder) {
            _cachedPtr = placeholder;
        }
        return EAssetResolveResult::Failed;
    }

    // Not ready yet — use placeholder, stay in Loading state
    if (_resolveState != EAssetResolveState::Loading) {
        _resolveState = EAssetResolveState::Loading;
        auto placeholder = TextureLibrary::get().getCheckerboardTexture();
        if (placeholder) {
            _cachedPtr = placeholder;
        }
    }
    return EAssetResolveResult::Pending;
}

inline EAssetResolveResult ModelRef::resolve()
{
    _path = AssetManager::normalizeAssetPath(_path);
    if (_path.empty()) {
        _resolveState = EAssetResolveState::Empty;
        return EAssetResolveResult::Failed;
    }

    if (_resolveState == EAssetResolveState::Ready && _cachedPtr) {
        const auto currentVersion = AssetManager::get()->getResourceVersion(_path);
        if (_resolvedVersion == currentVersion) {
            return EAssetResolveResult::Ready;
        }
        _cachedPtr.reset();
        _resolveState = EAssetResolveState::Dirty;
        YA_CORE_TRACE("ModelRef: version changed for '{}', re-resolving", _path);
    }

    const auto currentVersion = AssetManager::get()->getResourceVersion(_path);
    auto future = AssetManager::get()->loadModel(AssetManager::ModelLoadRequest{
        .filepath = _path,
    });
    if (future.isReady()) {
        _cachedPtr        = future.getShared();
        _resolveState     = EAssetResolveState::Ready;
        _resolvedVersion  = currentVersion;
        return EAssetResolveResult::Ready;
    }

    if (_resolveState != EAssetResolveState::Loading) {
        _resolveState = EAssetResolveState::Loading;
    }
    return EAssetResolveResult::Pending;
}

inline EAssetResolveResult MeshRef::resolve()
{
    _path = AssetManager::normalizeAssetPath(_path);
    // Mesh loading not implemented yet
    _resolveState = EAssetResolveState::Failed;
    UNIMPLEMENTED();
    return EAssetResolveResult::Failed;
}

inline void TextureRef::invalidate()
{
    _cachedPtr.reset();
    _resolveState = _path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty;
}

inline void TextureRef::set(const std::string& path, ya::Ptr<Texture> ptr)
{
    _path         = AssetManager::normalizeAssetPath(path);
    _cachedPtr    = std::move(ptr);
    _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
}

inline bool TextureRef::isStale() const
{
    if (_resolveState != EAssetResolveState::Ready || _path.empty()) return false;
    return _resolvedVersion != AssetManager::get()->getResourceVersion(_path);
}

inline void ModelRef::invalidate()
{
    _cachedPtr.reset();
    _resolveState = _path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty;
}

inline void ModelRef::set(const std::string& path, ya::Ptr<Model> ptr)
{
    _path         = AssetManager::normalizeAssetPath(path);
    _cachedPtr    = std::move(ptr);
    _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
}

inline bool ModelRef::isStale() const
{
    if (_resolveState != EAssetResolveState::Ready || _path.empty()) return false;
    return _resolvedVersion != AssetManager::get()->getResourceVersion(_path);
}

inline void MeshRef::invalidate()
{
    _cachedPtr.reset();
    _resolveState = _path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty;
}

inline void MeshRef::set(const std::string& path, ya::Ptr<Mesh> ptr)
{
    _path         = AssetManager::normalizeAssetPath(path);
    _cachedPtr    = std::move(ptr);
    _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
}

inline bool MeshRef::isStale() const
{
    if (_resolveState != EAssetResolveState::Ready || _path.empty()) return false;
    return _resolvedVersion != AssetManager::get()->getResourceVersion(_path);
}



// struct AssetBase
// {
//     stdpath _path;
// };


// // a light asset: only has path
// struct LightAsset : public AssetBase
// {
//     LightAsset() = default;
//     LightAsset(const stdpath &path) { _path = path; }
// };

} // namespace ya
