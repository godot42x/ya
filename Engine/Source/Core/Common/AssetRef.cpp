#include "Core/Common/AssetRef.h"
#include "Resource/AssetManager.h"
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
