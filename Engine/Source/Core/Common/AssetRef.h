#pragma once

#include "Core/Common/Types.h"
#include "Core/Delegate.h"
#include "Core/Reflection/Reflection.h"
#include <memory>
#include <string>



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
    explicit AssetRefBase(const std::string &path) : _path(path) {}

    virtual bool resolve() = 0;
    // {
    // resolve path from abs to engine?
    // }
    virtual void invalidate() = 0;

    const std::string &getPath() const { return _path; }
    bool               hasPath() const { return !_path.empty(); }
    void               setPath(const std::string &path)
    {
        _path = path;
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
        _path = path;
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


template <typename T>
struct TAssetRef : public AssetRefBase
{
    YA_REFLECT_BEGIN(TAssetRef<T>, AssetRefBase)
    YA_REFLECT_END()

    // TODO: Add reflection support for template classes
    ya::Ptr<T> _cachedPtr; // Runtime data: cached resource pointer (not serialized)
    EAssetResolveState _resolveState    = EAssetResolveState::Empty;
    uint64_t           _resolvedVersion = 0; // Version at which _cachedPtr was last resolved

    // Constructors
    TAssetRef() = default;
    explicit TAssetRef(const std::string &path) : AssetRefBase(path) {}
    TAssetRef(const std::string &path, ya::Ptr<T> ptr)
        : AssetRefBase(path), _cachedPtr(std::move(ptr))
    {
        _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
    }

    // Copy and move
    TAssetRef(const TAssetRef &other)
        : AssetRefBase(other), _cachedPtr(other._cachedPtr), _resolveState(other._resolveState)
    {
        // Note: onModified delegate is NOT copied intentionally
        // because it should not be shared between references
    }

    TAssetRef &operator=(const TAssetRef &other)
    {
        if (this != &other) {
            AssetRefBase::operator=(other);
            _cachedPtr = other._cachedPtr;
            _resolveState = other._resolveState;
        }
        return *this;
    }

    TAssetRef(TAssetRef &&other) noexcept            = default;
    TAssetRef &operator=(TAssetRef &&other) noexcept = default;

    // Access interface
    T                 *get() const { return _cachedPtr.get(); }
    ya::Ptr<T> getShared() const { return _cachedPtr; }
    // T       *operator->() const { return get(); }
    // T       &operator*() const { return *_cachedPtr; }
    // explicit operator bool() const { return _cachedPtr != nullptr; }

    bool isLoaded() const { return _resolveState == EAssetResolveState::Ready && _cachedPtr != nullptr; }
    bool               isLoading() const { return _resolveState == EAssetResolveState::Loading; }
    EAssetResolveState getResolveState() const { return _resolveState; }

    /**
     * @brief Check if cached pointer is stale (resource was reloaded after we resolved).
     *        Lightweight — only compares two integers. No map lookup when path is empty.
     */
    bool isStale() const;


    /**
     * @brief Resolve (load) the asset from path
     * Called by serialization system after deserialization
     * @return true if successfully loaded, false otherwise
     */
    bool resolve() override;

    void invalidate() override
    {
        _cachedPtr.reset();
        _resolveState = _path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty;
    }

    /**
     * @brief Set resource with path (updates both path and cached pointer)
     */
    void set(const std::string &path, ya::Ptr<T> ptr)
    {
        _path      = path;
        _cachedPtr = std::move(ptr);
        _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
    }

    /**
     * @brief Set resource from loaded asset (extracts path from asset if possible)
     */
    // void setFromAsset(std::shared_ptr<T> ptr)
    // {
    //     _cachedPtr = std::move(ptr);
    //     // Path should be set separately or extracted from asset metadata
    // }

    /**
     * @brief Clear the reference
     */
    // void clear()
    // {
    //     _path.clear();
    //     _cachedPtr.reset();
    // }

    /**
     * @brief Check equality (by path)
     */
    // bool operator==(const TAssetRef &other) const { return _path == other._path; }
    // bool operator!=(const TAssetRef &other) const { return _path != other._path; }
}; // namespace ya

// Common type aliases
using TextureRef = TAssetRef<Texture>;
using ModelRef   = TAssetRef<Model>;
using MeshRef    = TAssetRef<Mesh>;

// ============================================================================
// Asset Reference Resolution Interface
// ============================================================================

/**
 * @brief Interface for resolving asset references
 * Used by ReflectionSerializer to resolve TAssetRef types after deserialization
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
     * @param typeIndex Type index of the TAssetRef<T>
     * @param assetRefPtr Pointer to the TAssetRef instance
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
// TAssetRef<T>::resolve() Inline Implementations
// Include necessary headers for inline implementations
// ============================================================================
#include "Resource/AssetManager.h"
#include "Resource/TextureLibrary.h"
#include "Core/Log.h"

namespace ya
{

template <>
inline bool TAssetRef<Texture>::resolve()
{
    if (getPath().empty()) {
        _resolveState = EAssetResolveState::Empty;
        return false;
    }

    // Ready: check version to detect reloaded resources
    if (_resolveState == EAssetResolveState::Ready && _cachedPtr) {
        const auto currentVersion = AssetManager::get()->getResourceVersion(_path);
        if (_resolvedVersion == currentVersion) {
            return true;  // Up-to-date, fast path
        }
        // Version changed → stale pointer, force re-resolve
        _cachedPtr.reset();
        _resolveState = EAssetResolveState::Dirty;
        YA_CORE_TRACE("TAssetRef<Texture>: version changed for '{}', re-resolving", _path);
    }

    // Loading or Dirty: try to get the real texture from cache
    const auto currentVersion = AssetManager::get()->getResourceVersion(_path);
    auto future = AssetManager::get()->loadTexture(_path);
    if (future.isReady()) {
        _cachedPtr        = future.getShared();
        _resolveState     = EAssetResolveState::Ready;
        _resolvedVersion  = currentVersion;
        return true;
    }

    // Not ready yet — use placeholder, stay in Loading state
    if (_resolveState != EAssetResolveState::Loading) {
        _resolveState = EAssetResolveState::Loading;
        auto placeholder = TextureLibrary::get().getCheckerboardTexture();
        if (placeholder) {
            _cachedPtr = placeholder;
        }
        YA_CORE_TRACE("TAssetRef<Texture>: async loading '{}', using placeholder", _path);
    }
    return false;
}

template <>
inline bool TAssetRef<Model>::resolve()
{
    if (_path.empty()) {
        _resolveState = EAssetResolveState::Empty;
        return false;
    }

    if (_resolveState == EAssetResolveState::Ready && _cachedPtr) {
        const auto currentVersion = AssetManager::get()->getResourceVersion(_path);
        if (_resolvedVersion == currentVersion) {
            return true;
        }
        _cachedPtr.reset();
        _resolveState = EAssetResolveState::Dirty;
        YA_CORE_TRACE("TAssetRef<Model>: version changed for '{}', re-resolving", _path);
    }

    const auto currentVersion = AssetManager::get()->getResourceVersion(_path);
    auto future = AssetManager::get()->loadModel(_path);
    if (future.isReady()) {
        _cachedPtr        = future.getShared();
        _resolveState     = EAssetResolveState::Ready;
        _resolvedVersion  = currentVersion;
        return true;
    }

    if (_resolveState != EAssetResolveState::Loading) {
        _resolveState = EAssetResolveState::Loading;
        YA_CORE_TRACE("TAssetRef<Model>: async loading '{}'", _path);
    }
    return false;
}

template <>
inline bool TAssetRef<Mesh>::resolve()
{
    // Mesh loading not implemented yet
    _resolveState = EAssetResolveState::Failed;
    UNIMPLEMENTED();
    return true;
}

// ============================================================================
// TAssetRef<T>::isStale() — lightweight version check
// ============================================================================

template <typename T>
inline bool TAssetRef<T>::isStale() const
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
