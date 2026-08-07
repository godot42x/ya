#include "Core/Common/AssetRef.h"
#include "Resource/AssetManager.h"
#include "UI/Resource/TextureLibrary.h"
#include "Core/Log.h"
#include "Core/TypeIndex.h"
#include "Render/Core/Texture.h"
#include "Render/Mesh.h"
#include "Render/Model.h"


namespace ya
{


// ============================================================================
// DefaultAssetRefResolver Implementation
// ============================================================================

DefaultAssetRefResolver &DefaultAssetRefResolver::instance()
{
    static DefaultAssetRefResolver s_instance;
    return s_instance;
}

bool DefaultAssetRefResolver::isAssetRefType(type_index_t typeIndex) const
{
    // Check if typeIndex matches any concrete asset ref types
    static const type_index_t textureRefTypeIndex = ya::type_index_v<TextureRef>;
    static const type_index_t modelRefTypeIndex   = ya::type_index_v<ModelRef>;
    static const type_index_t meshRefTypeIndex    = ya::type_index_v<MeshRef>;

    return typeIndex == textureRefTypeIndex ||
           typeIndex == modelRefTypeIndex ||
           typeIndex == meshRefTypeIndex;
}

void DefaultAssetRefResolver::resolveAssetRef(type_index_t typeIndex, void *assetRefPtr) const
{
    static const type_index_t textureRefTypeIndex = ya::type_index_v<TextureRef>;
    static const type_index_t modelRefTypeIndex   = ya::type_index_v<ModelRef>;
    static const type_index_t meshRefTypeIndex    = ya::type_index_v<MeshRef>;

    if (typeIndex == textureRefTypeIndex) {
        static_cast<TextureRef *>(assetRefPtr)->resolve();
    }
    else if (typeIndex == modelRefTypeIndex) {
        static_cast<ModelRef *>(assetRefPtr)->resolve();
    }
    else if (typeIndex == meshRefTypeIndex) {
        static_cast<MeshRef *>(assetRefPtr)->resolve();
    }
    else {
        YA_CORE_WARN("DefaultAssetRefResolver: Unknown asset ref type index: {}", typeIndex);
    }
}

} // namespace ya
namespace ya
{


std::string AssetRefBase::normalizePath(std::string path)
{
    return AssetManager::normalizeAssetPath(std::move(path));
}

EAssetResolveResult TextureRef::resolve()
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

EAssetResolveResult ModelRef::resolve()
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

EAssetResolveResult MeshRef::resolve()
{
    _path = AssetManager::normalizeAssetPath(_path);
    // Mesh loading not implemented yet
    _resolveState = EAssetResolveState::Failed;
    UNIMPLEMENTED();
    return EAssetResolveResult::Failed;
}

void TextureRef::invalidate()
{
    _cachedPtr.reset();
    _resolveState = _path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty;
}

void TextureRef::set(const std::string& path, ya::Ptr<Texture> ptr)
{
    _path         = AssetManager::normalizeAssetPath(path);
    _cachedPtr    = std::move(ptr);
    _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
}

bool TextureRef::isStale() const
{
    if (_resolveState != EAssetResolveState::Ready || _path.empty()) return false;
    auto* const assets     = AssetManager::get();
    const auto  epoch      = assets->getResourceVersionEpoch();
    if (_lastCheckedEpoch == epoch) {
        return false;
    }
    _lastCheckedEpoch = epoch;
    return _resolvedVersion != assets->getResourceVersion(_path);
}

void ModelRef::invalidate()
{
    _cachedPtr.reset();
    _resolveState = _path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty;
}

void ModelRef::set(const std::string& path, ya::Ptr<Model> ptr)
{
    _path         = AssetManager::normalizeAssetPath(path);
    _cachedPtr    = std::move(ptr);
    _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
}

bool ModelRef::isStale() const
{
    if (_resolveState != EAssetResolveState::Ready || _path.empty()) return false;
    auto* const assets     = AssetManager::get();
    const auto  epoch      = assets->getResourceVersionEpoch();
    if (_lastCheckedEpoch == epoch) {
        return false;
    }
    _lastCheckedEpoch = epoch;
    return _resolvedVersion != assets->getResourceVersion(_path);
}

void MeshRef::invalidate()
{
    _cachedPtr.reset();
    _resolveState = _path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty;
}

void MeshRef::set(const std::string& path, ya::Ptr<Mesh> ptr)
{
    _path         = AssetManager::normalizeAssetPath(path);
    _cachedPtr    = std::move(ptr);
    _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
}

bool MeshRef::isStale() const
{
    if (_resolveState != EAssetResolveState::Ready || _path.empty()) return false;
    auto* const assets     = AssetManager::get();
    const auto  epoch      = assets->getResourceVersionEpoch();
    if (_lastCheckedEpoch == epoch) {
        return false;
    }
    _lastCheckedEpoch = epoch;
    return _resolvedVersion != assets->getResourceVersion(_path);
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
