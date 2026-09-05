#include "GameEditor/Inspector/PropertyCapabilityRegistry.h"

#include "Core/Common/AssetRef.h"
#include "reflects-core/lib.h"

#include <mutex>

namespace ya
{

PropertyCapabilityRegistry& PropertyCapabilityRegistry::instance()
{
    static PropertyCapabilityRegistry registry;
    return registry;
}

void PropertyCapabilityRegistry::registerAssetRef(type_index_t type, EEditorAssetPickerKind kind)
{
    _assetRefs[type] = kind;
}

std::optional<EEditorAssetPickerKind> PropertyCapabilityRegistry::assetRefKind(type_index_t type) const
{
    const auto it = _assetRefs.find(type);
    return it == _assetRefs.end() ? std::nullopt : std::optional{it->second};
}

void registerBuiltinPropertyCapabilities()
{
    static std::once_flag once;
    std::call_once(once, [] {
        auto& registry = PropertyCapabilityRegistry::instance();
        registry.registerAssetRef(refl::type_index_v<TextureRef>, EEditorAssetPickerKind::Texture);
        registry.registerAssetRef(refl::type_index_v<ModelRef>, EEditorAssetPickerKind::Model);
        registry.registerAssetRef(refl::type_index_v<MeshRef>, EEditorAssetPickerKind::Mesh);
    });
}

} // namespace ya
