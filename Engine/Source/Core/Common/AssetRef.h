#pragma once

#include "Core/Delegate.h"
#include "Core/Reflection/Reflection.h"
#include <memory>
#include <string>


namespace ya
{

// Forward declarations
class Texture;
class Model;
class Mesh;

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
    std::shared_ptr<T> _cachedPtr; // Runtime data: cached resource pointer (not serialized)

    // Constructors
    TAssetRef() = default;
    explicit TAssetRef(const std::string &path) : AssetRefBase(path) {}
    TAssetRef(const std::string &path, std::shared_ptr<T> ptr)
        : AssetRefBase(path), _cachedPtr(std::move(ptr))
    {
    }

    // Copy and move
    TAssetRef(const TAssetRef &other)
        : AssetRefBase(other), _cachedPtr(other._cachedPtr)
    {
        // Note: onModified delegate is NOT copied intentionally
        // because it should not be shared between references
    }
    
    TAssetRef &operator=(const TAssetRef &other)
    {
        if (this != &other) {
            AssetRefBase::operator=(other);
            _cachedPtr = other._cachedPtr;
        }
        return *this;
    }
    
    TAssetRef(TAssetRef &&other) noexcept = default;
    TAssetRef &operator=(TAssetRef &&other) noexcept = default;

    // Access interface
    T                 *get() const { return _cachedPtr.get(); }
    std::shared_ptr<T> getShared() const { return _cachedPtr; }
    // T       *operator->() const { return get(); }
    // T       &operator*() const { return *_cachedPtr; }
    // explicit operator bool() const { return _cachedPtr != nullptr; }

    bool isLoaded() const { return _cachedPtr != nullptr; }


    /**
     * @brief Resolve (load) the asset from path
     * Called by serialization system after deserialization
     * @return true if successfully loaded, false otherwise
     */
    bool resolve() override;

    void invalidate() override
    {
        _cachedPtr.reset();
    }

    /**
     * @brief Set resource with path (updates both path and cached pointer)
     */
    void set(const std::string &path, std::shared_ptr<T> ptr)
    {
        _path      = path;
        _cachedPtr = std::move(ptr);
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
#include "Core/AssetManager.h"
#include "Core/Log.h"

namespace ya
{

template <>
inline bool TAssetRef<Texture>::resolve()
{
    if (getPath().empty()) {
        return false;
    }
    if (_cachedPtr) {
        return true; // Already loaded
    }

    _cachedPtr = AssetManager::get()->loadTexture(_path);
    if (!_cachedPtr) {
        YA_CORE_WARN("TAssetRef<Texture>: Failed to load texture from path '{}'", _path);
        return false;
    }
    return true;
}

template <>
inline bool TAssetRef<Model>::resolve()
{
    if (_path.empty()) {
        return false;
    }
    if (_cachedPtr) {
        return true; // Already loaded
    }

    _cachedPtr = AssetManager::get()->loadModel(_path);
    if (!_cachedPtr) {
        YA_CORE_WARN("TAssetRef<Model>: Failed to load model from path '{}'", _path);
        return false;
    }
    // char a = "🫡";
    return true;
}

template <>
inline bool TAssetRef<Mesh>::resolve()
{
    // Mesh loading not implemented yet
    UNIMPLEMENTED();
    return true;
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
