#include "Core/Common/AssetRef.h"

#include "Core/Common/AssetTypeRegistry.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"
#include "Core/TypeIndex.h"

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

    // Game content is the bare default namespace: both the legacy Game:
    // mount form and the Content: mount form canonicalize to Content/....
    // Applied before VFS resolution (string inputs) and again after it
    // (absolute inputs resolve to mount:tail form), so both spellings of the
    // same file produce one canonical path.
    const auto normalizeGameContentMount = [](std::string value) -> std::string
    {
        if (value == "Game:Content") {
            return "Content";
        }
        if (value.starts_with("Game:Content/")) {
            return value.substr(std::string("Game:").size());
        }
        if (value == "Content:") {
            return "Content";
        }
        if (value.starts_with("Content:")) {
            return "Content/" + value.substr(std::string("Content:").size());
        }
        return value;
    };
    path = normalizeGameContentMount(std::move(path));

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
        path = normalizeGameContentMount(std::move(path));
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

bool isAssetRefType(type_index_t typeIndex)
{
    return AssetTypeRegistry::get().findByRefType(typeIndex).has_value();
}

namespace
{

struct BuiltinAssetTypeRegistration
{
    BuiltinAssetTypeRegistration()
    {
        AssetTypeRegistry::get().registerType(AssetTypeDesc{
            .name         = "Texture",
            .displayName  = "Select Texture",
            .extensions   = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds", ".hdr", ".ktx", ".ktx2"},
            .refType      = ya::type_index_v<TextureRef>,
            .resourceType = ya::type_index_v<Texture>,
            .store        = nullptr,
        });
        AssetTypeRegistry::get().registerType(AssetTypeDesc{
            .name         = "Model",
            .displayName  = "Select Model",
            .extensions   = {".obj", ".fbx", ".gltf", ".glb", ".dae"},
            .refType      = ya::type_index_v<ModelRef>,
            .resourceType = ya::type_index_v<Model>,
            .store        = nullptr,
        });
        AssetTypeRegistry::get().registerType(AssetTypeDesc{
            .name         = "Mesh",
            .displayName  = "Select Model",
            .extensions   = {".obj", ".fbx", ".gltf", ".glb", ".dae"},
            .refType      = ya::type_index_v<MeshRef>,
            .resourceType = ya::type_index_v<Mesh>,
            .store        = nullptr,
        });
    }
};

const BuiltinAssetTypeRegistration g_builtinAssetTypes;

} // namespace

void TextureRef::rebind()
{
    _handle.reset();
    if (_path.empty()) {
        return;
    }
    if (const auto* resolver = getAssetRefResolver()) {
        _handle = resolver->acquireTexture(_path);
    }
}

EAssetResolveState TextureRef::getResolveState() const
{
    if (_path.empty()) {
        return EAssetResolveState::Empty;
    }
    if (!_handle) {
        return EAssetResolveState::Failed;
    }
    switch (_handle->state) {
    case EAssetSlotState::Loading:
        return EAssetResolveState::Loading;
    case EAssetSlotState::Ready:
        return EAssetResolveState::Ready;
    case EAssetSlotState::Failed:
        return EAssetResolveState::Failed;
    }
    return EAssetResolveState::Failed;
}

void ModelRef::rebind()
{
    _handle.reset();
    if (_path.empty()) {
        return;
    }
    if (const auto* resolver = getAssetRefResolver()) {
        _handle = resolver->acquireModel(_path);
    }
}

EAssetResolveState ModelRef::getResolveState() const
{
    if (_path.empty()) {
        return EAssetResolveState::Empty;
    }
    if (!_handle) {
        return EAssetResolveState::Failed;
    }
    switch (_handle->state) {
    case EAssetSlotState::Loading:
        return EAssetResolveState::Loading;
    case EAssetSlotState::Ready:
        return EAssetResolveState::Ready;
    case EAssetSlotState::Failed:
        return EAssetResolveState::Failed;
    }
    return EAssetResolveState::Failed;
}

EAssetResolveState MeshRef::getResolveState() const
{
    // Meshes are not standalone assets (see MeshRef::_handle): a path-bearing
    // MeshRef has nothing to load from and reads as Failed.
    return _path.empty() ? EAssetResolveState::Empty : EAssetResolveState::Failed;
}

} // namespace ya
