#include "ECS/Component/Material/UnlitMaterialComponent.h"

#include "Core/Math/Math.h"
#include "Render/Resources/TextureSlotBinding.h"
#include "Render3D/Material/MaterialFactory.h"
#include "Render3D/Material/UnlitMaterial.h"

namespace ya
{

namespace unlit_detail
{

using TextureResource = UnlitMaterial::EResource;
using SlotEnum         = EUnlitMaterialTextureSlot;

TextureResource toTextureResource(SlotEnum slot)
{
    // Slot enum order mirrors UnlitMaterial::EResource (see header).
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

} // namespace unlit_detail

TextureSlot* UnlitMaterialComponent::getTextureSlotInternal(EUnlitMaterialTextureSlot resourceEnum)
{
    switch (unlit_detail::toTextureResource(resourceEnum)) {
    case UnlitMaterial::BaseColor0:
        return &_baseColor0Slot;
    case UnlitMaterial::BaseColor1:
        return &_baseColor1Slot;
    default:
        return nullptr;
    }
}

const TextureSlot* UnlitMaterialComponent::getTextureSlotInternal(EUnlitMaterialTextureSlot resourceEnum) const
{
    switch (unlit_detail::toTextureResource(resourceEnum)) {
    case UnlitMaterial::BaseColor0:
        return &_baseColor0Slot;
    case UnlitMaterial::BaseColor1:
        return &_baseColor1Slot;
    default:
        return nullptr;
    }
}

void UnlitMaterialComponent::syncParamsToMaterial()
{
    if (!getMaterial()) {
        return;
    }

    auto& runtimeParams       = getMaterial()->getParamsMut();
    runtimeParams.baseColor0  = _params.baseColor0;
    runtimeParams.baseColor1  = _params.baseColor1;
    runtimeParams.mixValue    = _params.mixValue;
    getMaterial()->setParamDirty();
}

void UnlitMaterialComponent::syncTextureSlot(EUnlitMaterialTextureSlot resourceEnum)
{
    if (!getMaterial()) {
        return;
    }

    const TextureSlot* slot = getTextureSlotInternal(resourceEnum);
    if (!slot) {
        return;
    }

    const auto resource = unlit_detail::toTextureResource(resourceEnum);

    if (slot->isReady()) {
        getMaterial()->setTextureBinding(resource, ya::slotToTextureBinding(*slot));
        getMaterial()->setTextureParam(
            resource,
            slot->isEnabledEffective(),
            FMath::build_transform_mat3(slot->uvOffset, slot->uvRotation, slot->uvScale));
        return;
    }

    if (!slot->hasPath()) {
        getMaterial()->clearTextureBinding(resource);
        getMaterial()->disableTextureParam(resource);
        return;
    }

    if (slot->textureRef.isLoading()) {
        // The slot observer re-queues this component when the update lands; the
        // semantic default (white) keeps the material complete while it waits.
        getMaterial()->setTextureBinding(resource, ya::loadingSlotFallback(false));
        return;
    }

    // Failed: an explicit, visible placeholder beats an invisible empty slot.
    YA_CORE_WARN("UnlitMaterialComponent: texture '{}' failed to load", slot->textureRef.getPath());
    getMaterial()->setTextureBinding(resource, ya::failedSlotFallback());
    getMaterial()->setTextureParam(
        resource,
        slot->isEnabledEffective(),
        FMath::build_transform_mat3(slot->uvOffset, slot->uvRotation, slot->uvScale));
}

EMaterialResolveResult UnlitMaterialComponent::resolve()
{
    // 1. Create runtime material if not exists (skip if using shared material)
    if (!_material) {
        unlit_detail::createOwnedMaterial<UnlitMaterialComponent, UnlitMaterial>(*this);

        if (!_material) {
            YA_CORE_ERROR("UnlitMaterialComponent: Failed to create runtime material");
            _resolveState = EMaterialResolveState::Failed;
            return EMaterialResolveResult::Failed;
        }
    }

    // 2. Sync params to runtime material (component authoring source -> runtime cache)
    syncParamsToMaterial();

    // 3. Sync every texture slot. A Loading slot binds its semantic default;
    // the slot observer re-queues this component when the fill lands.
    syncTextureSlots();

    _resolveState = EMaterialResolveState::Ready;
    return EMaterialResolveResult::Ready;
}

void UnlitMaterialComponent::syncTextureSlots()
{
    syncTextureSlot(EUnlitMaterialTextureSlot::BaseColor0);
    syncTextureSlot(EUnlitMaterialTextureSlot::BaseColor1);
}

} // namespace ya
