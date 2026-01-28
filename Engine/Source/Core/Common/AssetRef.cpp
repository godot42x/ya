#include "Core/Common/AssetRef.h"
#include "Core/AssetManager.h"
#include "Core/Log.h"
#include "Core/TypeIndex.h"
#include "Editor/TypeRenderer.h" // For DeferredModificationQueue
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
