#include "UnlitMaterialComponent.h"

#include "Core/Math/Math.h"
#include "RHI/Backend/TextureLibrary.h"
#include "GUI/Resources/TextureSlotBinding.h"
#include "Render3D/Material/MaterialFactory.h"
#include "Render3D/Material/UnlitMaterial.h"

#include <string_view>


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

bool containsPathToken(std::string_view propPath, std::string_view token)
{
    return propPath.find(token) != std::string_view::npos;
}

TextureResource textureResourceFromPath(std::string_view propPath)
{
    if (propPath.starts_with("_baseColor0Slot")) {
        return TextureResource::BaseColor0;
    }
    if (propPath.starts_with("_baseColor1Slot")) {
        return TextureResource::BaseColor1;
    }

    return TextureResource::Count;
}

bool isTextureRefPath(std::string_view propPath)
{
    return containsPathToken(propPath, "textureRef");
}

bool isParamPath(std::string_view propPath)
{
    return propPath.starts_with("_params");
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

UnlitMaterialComponent::PropertyChangeSummary UnlitMaterialComponent::summarizePropertyChanges(const std::vector<std::string>& propPaths)
{
    PropertyChangeSummary summary;

    for (const auto& propPath : propPaths) {
        auto resource = unlit_detail::textureResourceFromPath(propPath);
        if (resource == unlit_detail::TextureResource::Count) {
            continue;
        }

        auto index                  = static_cast<size_t>(resource);
        summary.touchedSlots[index] = true;
        summary.hasTextureSlotChange = true;
        summary.hasTextureResourceChange = summary.hasTextureResourceChange || unlit_detail::isTextureRefPath(propPath);
    }

    return summary;
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

    if (slot->isReady()) {
        getMaterial()->setTextureBinding(unlit_detail::toTextureResource(resourceEnum), ya::slotToTextureBinding(*slot));
        getMaterial()->setTextureParam(
            unlit_detail::toTextureResource(resourceEnum),
            slot->isEnabledEffective(),
            FMath::build_transform_mat3(slot->uvOffset, slot->uvRotation, slot->uvScale));
    }
    else if (slot->hasPath() && slot->textureRef.isLoading()) {
        // Texture is being reloaded — old GPU resources may be destroyed.
        // Use placeholder to avoid null imageView in descriptor writes.
        auto placeholder = TextureLibrary::get().getCheckerboardTexture();
        auto sampler     = TextureLibrary::get().getDefaultSampler();
        if (placeholder && sampler) {
            getMaterial()->setTextureBinding(unlit_detail::toTextureResource(resourceEnum), TextureBinding{placeholder, sampler});
            getMaterial()->setTextureParam(
                unlit_detail::toTextureResource(resourceEnum),
                slot->isEnabledEffective(),
                FMath::build_transform_mat3(slot->uvOffset, slot->uvRotation, slot->uvScale));
        }
        else {
            getMaterial()->clearTextureBinding(unlit_detail::toTextureResource(resourceEnum));
            getMaterial()->disableTextureParam(unlit_detail::toTextureResource(resourceEnum));
        }
    }
    else {
        getMaterial()->clearTextureBinding(unlit_detail::toTextureResource(resourceEnum));
        getMaterial()->disableTextureParam(unlit_detail::toTextureResource(resourceEnum));
    }
}

EMaterialResolveResult UnlitMaterialComponent::resolve()
{
    if (_resolveState == EMaterialResolveState::Ready) {
        return EMaterialResolveResult::Ready;
    }

    _resolveState = EMaterialResolveState::Resolving;

    bool success = true;
    bool hasPendingTextures = false;

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

    // 3. Resolve texture slots and build texture bindings
    // NOTE: Do NOT clearTextureBindings() — keep old bindings while async reload in flight.

    auto resolveSlot = [&](TextureSlot& slot, const char* name) {
        if (!slot.hasPath() || slot.isReady()) {
            return;
        }

        const auto result = slot.resolve();
        if (result == EAssetResolveResult::Ready) {
            return;
        }

        if (result == EAssetResolveResult::Pending) {
            hasPendingTextures = true;
            return;
        }

        YA_CORE_WARN("UnlitMaterialComponent: Failed to resolve {} texture slot", name);
        success = false;
    };

    resolveSlot(_baseColor0Slot, "baseColor0");
    resolveSlot(_baseColor1Slot, "baseColor1");

    syncTextureSlots();

    if (!success) {
        _resolveState = EMaterialResolveState::Failed;
        return EMaterialResolveResult::Failed;
    }

    _resolveState = hasPendingTextures ? EMaterialResolveState::Resolving : EMaterialResolveState::Ready;
    return hasPendingTextures ? EMaterialResolveResult::Pending : EMaterialResolveResult::Ready;
}

void UnlitMaterialComponent::onPropertyChanged(const std::string& propPath)
{
    onPropertiesChanged({propPath});
}

void UnlitMaterialComponent::onPropertiesChanged(const std::vector<std::string>& propPaths)
{
    bool hasParamChange = false;
    for (const auto& propPath : propPaths) {
        hasParamChange = hasParamChange || unlit_detail::isParamPath(propPath);
    }

    const auto summary = summarizePropertyChanges(propPaths);

    if (!summary.hasTextureSlotChange && !hasParamChange) {
        return;
    }

    if (hasParamChange) {
        syncParamsToMaterial();
    }

    if (summary.hasTextureResourceChange) {
        invalidate();
    }

    for (size_t index = 0; index < summary.touchedSlots.size(); ++index) {
        if (!summary.touchedSlots[index]) {
            continue;
        }
        syncTextureSlot(static_cast<EUnlitMaterialTextureSlot>(index));
    }
}


void UnlitMaterialComponent::syncTextureSlots()
{
    syncTextureSlot(EUnlitMaterialTextureSlot::BaseColor0);
    syncTextureSlot(EUnlitMaterialTextureSlot::BaseColor1);
}


} // namespace ya
