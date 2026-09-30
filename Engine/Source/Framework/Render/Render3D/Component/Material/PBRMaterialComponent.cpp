#include "ECS/Component/Material/PBRMaterialComponent.h"

#include "Render/Resources/TextureSlotBinding.h"
#include "Render3D/Material/MaterialFactory.h"
#include "Render3D/Material/PBRMaterial.h"
#include "Resource/Core/Model/MaterialData.h"

namespace ya
{

namespace detail_pbr
{

using TextureResource = PBRMaterial::EResource;
using SlotEnum         = EPBRMaterialTextureSlot;

TextureResource toTextureResource(SlotEnum slot)
{
    // Slot enum order mirrors PBRMaterial::EResource (see header).
    return static_cast<TextureResource>(slot);
}

template <typename ComponentType, typename MaterialType>
MaterialType* createOwnedMaterial(ComponentType& comp)
{
    comp.releaseMaterial();
    std::string matLabel = typeid(MaterialType).name() + std::to_string(reinterpret_cast<uintptr_t>(&comp));
    comp._material       = MaterialFactory::get()->createMaterial<MaterialType>(matLabel);
    comp._bSharedMaterial = false; // Created our own material
    return static_cast<MaterialType*>(comp._material);
}

} // namespace detail_pbr

TextureSlot* PBRMaterialComponent::getTextureSlotInternal(EPBRMaterialTextureSlot resourceEnum)
{
    switch (detail_pbr::toTextureResource(resourceEnum)) {
    case PBRMaterial::AlbedoTexture:    return &_albedoSlot;
    case PBRMaterial::NormalTexture:    return &_normalSlot;
    case PBRMaterial::MetallicTexture:  return &_metallicSlot;
    case PBRMaterial::RoughnessTexture: return &_roughnessSlot;
    case PBRMaterial::AOTexture:        return &_aoSlot;
    default: return nullptr;
    }
}

const TextureSlot* PBRMaterialComponent::getTextureSlotInternal(EPBRMaterialTextureSlot resourceEnum) const
{
    switch (detail_pbr::toTextureResource(resourceEnum)) {
    case PBRMaterial::AlbedoTexture:    return &_albedoSlot;
    case PBRMaterial::NormalTexture:    return &_normalSlot;
    case PBRMaterial::MetallicTexture:  return &_metallicSlot;
    case PBRMaterial::RoughnessTexture: return &_roughnessSlot;
    case PBRMaterial::AOTexture:        return &_aoSlot;
    default: return nullptr;
    }
}

void PBRMaterialComponent::syncParamsToMaterial()
{
    if (!getMaterial()) return;

    auto& runtimeParams     = getMaterial()->getParamsMut();
    runtimeParams.albedo    = _params.albedo;
    runtimeParams.metallic  = _params.metallic;
    runtimeParams.roughness = _params.roughness;
    runtimeParams.ao        = _params.ao;
    getMaterial()->setParamDirty();
}

void PBRMaterialComponent::importParamsFromDescriptor(const MaterialData& matData)
{
    _params.albedo    = glm::vec3(matData.getParam<glm::vec4>(MatParam::BaseColor, glm::vec4(1.0f)));
    _params.metallic  = matData.getParam<float>(MatParam::Metallic, 0.0f);
    _params.roughness = matData.getParam<float>(MatParam::Roughness, 0.5f);
    _params.ao        = 1.0f; // AO is typically baked in textures
}

void PBRMaterialComponent::syncTextureSlot(EPBRMaterialTextureSlot resourceEnum)
{
    if (!getMaterial()) return;

    const TextureSlot* slot = getTextureSlotInternal(resourceEnum);
    if (!slot) return;

    const auto resource = detail_pbr::toTextureResource(resourceEnum);

    // PBR's setTextureParam takes an effective slot (bEnable resolved).
    const auto effectiveParam = [&]() {
        TextureSlot effectiveSlot = *slot;
        effectiveSlot.bEnable     = slot->isEnabledEffective();
        return effectiveSlot;
    };

    if (slot->isReady()) {
        getMaterial()->setTextureBinding(resource, ya::slotToTextureBinding(*slot));
        getMaterial()->setTextureParam(resource, effectiveParam());
        return;
    }

    if (!slot->hasPath()) {
        getMaterial()->disableTextureParams(resource);
        getMaterial()->clearTextureBinding(resource);
        return;
    }

    if (slot->textureRef.isLoading()) {
        // The slot observer re-queues this component when the update lands; the
        // semantic default (white, flat normal for normal maps) keeps the
        // material complete and shadable while it waits.
        getMaterial()->setTextureBinding(resource, ya::loadingSlotFallback(resourceEnum == EPBRMaterialTextureSlot::Normal));
        getMaterial()->setTextureParam(resource, effectiveParam());
        return;
    }

    // Failed: an explicit, visible placeholder beats an invisible empty slot.
    YA_CORE_WARN("PBRMaterialComponent: texture '{}' failed to load", slot->textureRef.getPath());
    getMaterial()->setTextureBinding(resource, ya::failedSlotFallback());
    getMaterial()->setTextureParam(resource, effectiveParam());
}

EMaterialResolveResult PBRMaterialComponent::resolve()
{
    // 1. Create runtime material if not exists
    if (!_material) {
        detail_pbr::createOwnedMaterial<PBRMaterialComponent, PBRMaterial>(*this);
        if (!_material) {
            YA_CORE_ERROR("PBRMaterialComponent: Failed to create runtime material");
            _resolveState = EMaterialResolveState::Failed;
            return EMaterialResolveResult::Failed;
        }
    }

    // 2. Sync params
    syncParamsToMaterial();

    // 3. Sync every texture slot. A Loading slot binds its semantic default;
    // the slot observer re-queues this component when the fill lands.
    syncTextureSlots();

    _resolveState = EMaterialResolveState::Ready;
    return EMaterialResolveResult::Ready;
}

void PBRMaterialComponent::syncTextureSlots()
{
    syncTextureSlot(EPBRMaterialTextureSlot::Albedo);
    syncTextureSlot(EPBRMaterialTextureSlot::Normal);
    syncTextureSlot(EPBRMaterialTextureSlot::Metallic);
    syncTextureSlot(EPBRMaterialTextureSlot::Roughness);
    syncTextureSlot(EPBRMaterialTextureSlot::AO);
}

void PBRMaterialComponent::importFromDescriptor(const MaterialData& matData)
{
    importParamsFromDescriptor(matData);

    _albedoSlot.textureRef.setPath("");
    _normalSlot.textureRef.setPath("");
    _metallicSlot.textureRef.setPath("");
    _roughnessSlot.textureRef.setPath("");
    _aoSlot.textureRef.setPath("");

    if (matData.hasTexture(MatTexture::Albedo)) {
        _albedoSlot.textureRef.setPath(matData.resolveTexturePath(MatTexture::Albedo));
    }
    else if (matData.hasTexture(MatTexture::Diffuse)) {
        _albedoSlot.textureRef.setPath(matData.resolveTexturePath(MatTexture::Diffuse));
    }
    if (matData.hasTexture(MatTexture::Normal)) {
        _normalSlot.textureRef.setPath(matData.resolveTexturePath(MatTexture::Normal));
    }
    if (matData.hasTexture(MatTexture::Metallic)) {
        _metallicSlot.textureRef.setPath(matData.resolveTexturePath(MatTexture::Metallic));
    }
    if (matData.hasTexture(MatTexture::Roughness)) {
        _roughnessSlot.textureRef.setPath(matData.resolveTexturePath(MatTexture::Roughness));
    }
    if (matData.hasTexture(MatTexture::AO)) {
        _aoSlot.textureRef.setPath(matData.resolveTexturePath(MatTexture::AO));
    }

    if (matData.hasTexture(MatTexture::MetallicRoughness) &&
        !matData.hasTexture(MatTexture::Metallic) &&
        !matData.hasTexture(MatTexture::Roughness)) {
        YA_CORE_WARN("PBRMaterialComponent: '{}' has packed metallicRoughness texture '{}', but current PBR path only consumes separate metallic/roughness slots",
                     matData.name,
                     matData.resolveTexturePath(MatTexture::MetallicRoughness));
    }

    invalidate();
}

void PBRMaterialComponent::importFromDescriptorWithSharedMaterial(const MaterialData& matData, PBRMaterial* sharedMaterial)
{
    if (!sharedMaterial) {
        YA_CORE_ERROR("PBRMaterialComponent::importFromDescriptorWithSharedMaterial: sharedMaterial is null");
        return;
    }

    importFromDescriptor(matData);
    setSharedMaterial(sharedMaterial);
}

} // namespace ya
