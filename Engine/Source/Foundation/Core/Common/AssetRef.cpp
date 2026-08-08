#include "Foundation/Core/Common/AssetRef.h"

#include "Foundation/Core/Log.h"
#include "Foundation/Core/System/VirtualFileSystem.h"
#include "Foundation/Core/TypeIndex.h"

#include <algorithm>
#include <filesystem>

namespace ya
{

// ============================================================================
// Canonical asset path (shared by AssetRef and the resource layer; the
// resource layer's AssetManager::normalizeAssetPath forwards here).
// ============================================================================

std::string canonicalizeAssetPath(std::string path)
{
    if (path.empty()) {
        return path;
    }

    std::replace(path.begin(), path.end(), '\\', '/');
    path = std::filesystem::path(path).lexically_normal().generic_string();

    const auto normalizeMountedPath = [](std::string value) -> std::string
    {
        const auto separator = value.find(':');
        if (separator == std::string::npos || value.find('/') <= separator) {
            return value;
        }

        const auto mount = value.substr(0, separator + 1);
        const auto tail  = std::filesystem::path(value.substr(separator + 1)).lexically_normal().generic_string();
        return mount + tail;
    };

    path = normalizeMountedPath(std::move(path));

    if (path == "Engine/Content" || path.starts_with("Engine/Content/")) {
        path = "Engine:" + path.substr(std::string("Engine/").size());
    }

    if (path == "Game:Content") {
        path = "Content";
    }
    else if (path.starts_with("Game:Content/")) {
        path = path.substr(std::string("Game:").size());
    }

    if (path.starts_with("Engine:Content/Content/")) {
        path = "Engine:Content/" + path.substr(std::string("Engine:Content/Content/").size());
    }
    if (path.starts_with("Content/Content/")) {
        path = "Content/" + path.substr(std::string("Content/Content/").size());
    }

    if (auto* vfs = VirtualFileSystem::get()) {
        path = vfs->toVfsPath(path);
        std::replace(path.begin(), path.end(), '\\', '/');
        path = normalizeMountedPath(std::move(path));
    }

    return path;
}

// ============================================================================
// Asset-ref resolver registration (Core keeps the slot; the resource layer
// installs the engine implementation at static-init time).
// ============================================================================

namespace
{
const IAssetRefResolver* g_assetRefResolver = nullptr;
}

const IAssetRefResolver* getAssetRefResolver()
{
    return g_assetRefResolver;
}

void setAssetRefResolver(const IAssetRefResolver* resolver)
{
    g_assetRefResolver = resolver;
}

// ============================================================================
// AssetRefBase
// ============================================================================

std::string AssetRefBase::normalizePath(std::string path)
{
    return canonicalizeAssetPath(std::move(path));
}

// ============================================================================
// TextureRef / ModelRef / MeshRef
//
// The concrete ref types live in Core (GUI and scene code reference them),
// so their vtables and state transitions must resolve without the resource
// layer. Actual loading is delegated to the installed asset-ref resolver.
// ============================================================================

namespace
{

template <typename T>
EAssetResolveResult resolveViaRegistry(T& ref)
{
    if (const auto* resolver = getAssetRefResolver()) {
        resolver->resolveAssetRef(ya::type_index_v<T>, &ref);
    }
    else if (ref.getPath().empty()) {
        ref._resolveState = EAssetResolveState::Empty;
    }
    else {
        // Pure GUI host without a resource layer: mark failed instead of
        // silently staying dirty forever.
        ref._resolveState = EAssetResolveState::Failed;
    }
    return ref._resolveState == EAssetResolveState::Ready ? EAssetResolveResult::Ready
         : ref._resolveState == EAssetResolveState::Loading ? EAssetResolveResult::Pending
                                                           : EAssetResolveResult::Failed;
}

} // namespace

EAssetResolveResult TextureRef::resolve()
{
    _path = canonicalizeAssetPath(_path);
    return resolveViaRegistry(*this);
}

EAssetResolveResult ModelRef::resolve()
{
    _path = canonicalizeAssetPath(_path);
    return resolveViaRegistry(*this);
}

EAssetResolveResult MeshRef::resolve()
{
    _path = canonicalizeAssetPath(_path);
    return resolveViaRegistry(*this);
}

void TextureRef::invalidate()
{
    _cachedPtr.reset();
    _resolveState = _path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty;
}

void TextureRef::set(const std::string& path, ya::Ptr<Texture> ptr)
{
    _path         = canonicalizeAssetPath(path);
    _cachedPtr    = std::move(ptr);
    _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
}

bool TextureRef::isStale() const
{
    if (_resolveState != EAssetResolveState::Ready || _path.empty()) {
        return false;
    }
    if (const auto* resolver = getAssetRefResolver()) {
        return resolver->isAssetRefStale(ya::type_index_v<TextureRef>, this);
    }
    return false;
}

void ModelRef::invalidate()
{
    _cachedPtr.reset();
    _resolveState = _path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty;
}

void ModelRef::set(const std::string& path, ya::Ptr<Model> ptr)
{
    _path         = canonicalizeAssetPath(path);
    _cachedPtr    = std::move(ptr);
    _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
}

bool ModelRef::isStale() const
{
    if (_resolveState != EAssetResolveState::Ready || _path.empty()) {
        return false;
    }
    if (const auto* resolver = getAssetRefResolver()) {
        return resolver->isAssetRefStale(ya::type_index_v<ModelRef>, this);
    }
    return false;
}

void MeshRef::invalidate()
{
    _cachedPtr.reset();
    _resolveState = _path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty;
}

void MeshRef::set(const std::string& path, ya::Ptr<Mesh> ptr)
{
    _path         = canonicalizeAssetPath(path);
    _cachedPtr    = std::move(ptr);
    _resolveState = _cachedPtr ? EAssetResolveState::Ready : (_path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Dirty);
}

bool MeshRef::isStale() const
{
    if (_resolveState != EAssetResolveState::Ready || _path.empty()) {
        return false;
    }
    if (const auto* resolver = getAssetRefResolver()) {
        return resolver->isAssetRefStale(ya::type_index_v<MeshRef>, this);
    }
    return false;
}

} // namespace ya
