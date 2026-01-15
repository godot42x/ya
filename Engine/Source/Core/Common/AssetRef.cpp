#include "Core/Common/AssetRef.h"
#include "Core/AssetManager.h"
#include "Core/Log.h"
#include "Core/TypeIndex.h"
#include "Render/Core/Texture.h"
#include "Render/Mesh.h"
#include "Render/Model.h"


namespace ya
{

// ============================================================================
// TAssetRef<T>::resolve() Specializations
// ============================================================================

template <>
bool TAssetRef<Texture>::resolve()
{
    if (_path.empty()) {
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
bool TAssetRef<Model>::resolve()
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
    return true;
}

template <>
bool TAssetRef<Mesh>::resolve()
{
    if (_path.empty()) {
        return false;
    }
    if (_cachedPtr) {
        return true; // Already loaded
    }

    // Mesh loading typically comes from Model, need special handling
    // For now, log warning - may need to implement mesh-specific loading
    YA_CORE_WARN("TAssetRef<Mesh>: Direct mesh loading not implemented. Path: '{}'", _path);
    return false;
}

// ============================================================================
// DefaultAssetRefResolver Implementation
// ============================================================================

DefaultAssetRefResolver &DefaultAssetRefResolver::instance()
{
    static DefaultAssetRefResolver s_instance;
    return s_instance;
}

bool DefaultAssetRefResolver::isAssetRefType(uint32_t typeIndex) const
{
    // Check if typeIndex matches any registered TAssetRef<T> types
    static const uint32_t textureRefTypeIndex = ya::type_index_v<TextureRef>;
    static const uint32_t modelRefTypeIndex   = ya::type_index_v<ModelRef>;
    static const uint32_t meshRefTypeIndex    = ya::type_index_v<MeshRef>;

    return typeIndex == textureRefTypeIndex ||
           typeIndex == modelRefTypeIndex ||
           typeIndex == meshRefTypeIndex;
}

void DefaultAssetRefResolver::resolveAssetRef(uint32_t typeIndex, void *assetRefPtr) const
{
    static const uint32_t textureRefTypeIndex = ya::type_index_v<TextureRef>;
    static const uint32_t modelRefTypeIndex   = ya::type_index_v<ModelRef>;
    static const uint32_t meshRefTypeIndex    = ya::type_index_v<MeshRef>;

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
