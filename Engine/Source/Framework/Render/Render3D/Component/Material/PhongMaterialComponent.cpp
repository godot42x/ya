#include "PhongMaterialComponent.h"

#include "Core/Math/Math.h"
#include "RHI/Backend/TextureLibrary.h"
#include "Render/Resources/TextureSlotBinding.h"
#include "Render3D/Material/MaterialFactory.h"
#include "Render3D/Material/PhongMaterial.h"
#include "Resource/Core/Model/MaterialData.h"

#include <string_view>


namespace ya
{

namespace detail_phong
{

using TextureResource = PhongMaterial::EResource;
using SlotEnum         = EPhongMaterialTextureSlot;

TextureResource toTextureResource(SlotEnum slot)
{
    // Slot enum order mirrors PhongMaterial::EResource (see header).
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
    if (propPath.starts_with("_diffuseSlot")) {
        return TextureResource::DiffuseTexture;
    }
    if (propPath.starts_with("_specularSlot")) {
        return TextureResource::SpecularTexture;
    }
    if (propPath.starts_with("_reflectionSlot")) {
        return TextureResource::ReflectionTexture;
    }
    if (propPath.starts_with("_normalSlot")) {
        return TextureResource::NormalTexture;
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

} // namespace detail_phong

TextureSlot* PhongMaterialComponent::getTextureSlotInternal(EPhongMaterialTextureSlot resourceEnum)
{
    switch (detail_phong::toTextureResource(resourceEnum)) {
    case PhongMaterial::DiffuseTexture:
        return &_diffuseSlot;
    case PhongMaterial::SpecularTexture:
        return &_specularSlot;
    case PhongMaterial::ReflectionTexture:
        return &_reflectionSlot;
    case PhongMaterial::NormalTexture:
        return &_normalSlot;
    default:
        return nullptr;
    }
}

const TextureSlot* PhongMaterialComponent::getTextureSlotInternal(EPhongMaterialTextureSlot resourceEnum) const
{
    switch (detail_phong::toTextureResource(resourceEnum)) {
    case PhongMaterial::DiffuseTexture:
        return &_diffuseSlot;
    case PhongMaterial::SpecularTexture:
        return &_specularSlot;
    case PhongMaterial::ReflectionTexture:
        return &_reflectionSlot;
    case PhongMaterial::NormalTexture:
        return &_normalSlot;
    default:
        return nullptr;
    }
}

PhongMaterialComponent::PropertyChangeSummary PhongMaterialComponent::summarizePropertyChanges(const std::vector<std::string>& propPaths)
{
    PropertyChangeSummary summary;

    for (const auto& propPath : propPaths) {
        auto resource = detail_phong::textureResourceFromPath(propPath);
        if (resource == detail_phong::TextureResource::Count) {
            continue;
        }

        auto index                       = static_cast<size_t>(resource);
        summary.touchedSlots[index]      = true;
        summary.hasTextureSlotChange     = true;
        summary.hasTextureResourceChange = summary.hasTextureResourceChange || detail_phong::isTextureRefPath(propPath);
    }

    return summary;
}

void PhongMaterialComponent::syncParamsToMaterial()
{
    if (!getMaterial()) {
        return;
    }

    auto& runtimeParams     = getMaterial()->getParamsMut();
    runtimeParams.ambient   = _params.ambient;
    runtimeParams.diffuse   = _params.diffuse;
    runtimeParams.specular  = _params.specular;
    runtimeParams.shininess = _params.shininess;
    getMaterial()->setParamDirty();
}

void PhongMaterialComponent::importParamsFromDescriptor(const MaterialData& matData)
{
    _params.ambient   = matData.getParam<glm::vec3>(MatParam::Ambient, glm::vec3(0.1f));
    _params.diffuse   = glm::vec3(matData.getParam<glm::vec4>(MatParam::BaseColor, glm::vec4(1.0f)));
    _params.specular  = matData.getParam<glm::vec3>(MatParam::Specular, glm::vec3(0.5f));
    _params.shininess = matData.getParam<float>(MatParam::Shininess, 32.0f);
}

void PhongMaterialComponent::syncTextureSlot(EPhongMaterialTextureSlot resourceEnum)
{
    if (!getMaterial()) {
        return;
    }

    const TextureSlot* slot = getTextureSlotInternal(resourceEnum);
    if (!slot) {
        return;
    }

    if (slot->isReady()) {
        getMaterial()->setTextureBinding(detail_phong::toTextureResource(resourceEnum), ya::slotToTextureBinding(*slot));
        getMaterial()->setTextureParam(
            detail_phong::toTextureResource(resourceEnum),
            slot->isEnabledEffective(),
            FMath::build_transform_mat3(slot->uvOffset, slot->uvRotation, slot->uvScale));
    }
    else if (slot->hasPath() && slot->textureRef.isLoading()) {
        // Texture is being reloaded asynchronously — old GPU resources may already
        // be destroyed by DeferredDeletionQueue so we can't keep the old binding.
        // Use a placeholder texture to avoid null imageView in descriptor writes.
        auto placeholder = TextureLibrary::get().getCheckerboardTexture();
        auto sampler     = TextureLibrary::get().getDefaultSampler();
        if (placeholder && sampler) {
            getMaterial()->setTextureBinding(detail_phong::toTextureResource(resourceEnum), TextureBinding{placeholder, sampler});
        }
        else {
            getMaterial()->clearTextureBinding(detail_phong::toTextureResource(resourceEnum));
            getMaterial()->disableTextureParam(detail_phong::toTextureResource(resourceEnum));
        }
    }
    else {
        getMaterial()->clearTextureBinding(detail_phong::toTextureResource(resourceEnum));
        getMaterial()->disableTextureParam(detail_phong::toTextureResource(resourceEnum));
    }
}

EMaterialResolveResult PhongMaterialComponent::resolve()
{
    if (_resolveState == EMaterialResolveState::Ready) {
        return EMaterialResolveResult::Ready;
    }

    _resolveState = EMaterialResolveState::Resolving;

    bool success = true;
    bool hasPendingTextures = false;

    // 1. Create runtime material if not exists (skip if using shared material)
    if (!_material) {
        detail_phong::createOwnedMaterial<PhongMaterialComponent, PhongMaterial>(*this);

        if (!_material) {
            YA_CORE_ERROR("PhongMaterialComponent: Failed to create runtime material");
            _resolveState = EMaterialResolveState::Failed;
            return EMaterialResolveResult::Failed;
        }
    }

    // 2. Sync params to runtime material (component authoring source -> runtime cache)
    syncParamsToMaterial();

    // 3. Resolve texture slots and build texture bindings
    // NOTE: Do NOT clearTextureBindings() here — if a texture is being reloaded
    // asynchronously, we want to keep the old binding until the new one is ready.
    // syncTextureSlot() handles the per-slot logic.

    // for (auto &[key, slot] : _textureSlots) {
    //     if (slot.textureRef.hasPath() && !slot.isLoaded()) {
    //         if (!slot.resolve()) {
    //             YA_CORE_WARN("PhongMaterialComponent: Failed to resolve texture slot {} ({})",
    //                          getMaterial()->getTextureSlotName(key),
    //                          slot.textureRef.getPath());
    //             success = false;
    //             continue;
    //         }
    //     }
    // }
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

        YA_CORE_WARN("PhongMaterialComponent: Failed to resolve {} texture slot", name);
        success = false;
    };

    resolveSlot(_diffuseSlot, "diffuse");
    resolveSlot(_specularSlot, "specular");
    resolveSlot(_reflectionSlot, "reflection");
    resolveSlot(_normalSlot, "normal");

    syncTextureSlots();

    if (!success) {
        _resolveState = EMaterialResolveState::Failed;
        return EMaterialResolveResult::Failed;
    }

    _resolveState = hasPendingTextures ? EMaterialResolveState::Resolving : EMaterialResolveState::Ready;
    return hasPendingTextures ? EMaterialResolveResult::Pending : EMaterialResolveResult::Ready;
}

void PhongMaterialComponent::onPropertyChanged(const std::string& propPath)
{
    onPropertiesChanged({propPath});
}

void PhongMaterialComponent::onPropertiesChanged(const std::vector<std::string>& propPaths)
{
    bool hasParamChange = false;
    for (const auto& propPath : propPaths) {
        hasParamChange = hasParamChange || detail_phong::isParamPath(propPath);
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
        syncTextureSlot(static_cast<EPhongMaterialTextureSlot>(index));
    }
}


void PhongMaterialComponent::syncTextureSlots()
{
    syncTextureSlot(EPhongMaterialTextureSlot::Diffuse);
    syncTextureSlot(EPhongMaterialTextureSlot::Specular);
    syncTextureSlot(EPhongMaterialTextureSlot::Reflection);
    syncTextureSlot(EPhongMaterialTextureSlot::Normal);
}

void PhongMaterialComponent::importFromDescriptor(const MaterialData& matData)
{
    importParamsFromDescriptor(matData);

    _diffuseSlot.textureRef.setPathWithoutNotify("");
    _specularSlot.textureRef.setPathWithoutNotify("");
    _reflectionSlot.textureRef.setPathWithoutNotify("");
    _normalSlot.textureRef.setPathWithoutNotify("");

    if (matData.hasTexture(MatTexture::Diffuse)) {
        std::string path = matData.resolveTexturePath(MatTexture::Diffuse);
        _diffuseSlot.textureRef.setPathWithoutNotify(path);
    }

    if (matData.hasTexture(MatTexture::Specular)) {
        std::string path = matData.resolveTexturePath(MatTexture::Specular);
        _specularSlot.textureRef.setPathWithoutNotify(path);
    }

    if (matData.hasTexture(MatTexture::Normal)) {
        std::string path = matData.resolveTexturePath(MatTexture::Normal);
        _normalSlot.textureRef.setPathWithoutNotify(path);
    }

    invalidate();
}

void PhongMaterialComponent::importFromDescriptorWithSharedMaterial(const MaterialData& matData, PhongMaterial* sharedMaterial)
{
    if (!sharedMaterial) {
        YA_CORE_ERROR("PhongMaterialComponent::importFromDescriptorWithSharedMaterial: sharedMaterial is null");
        return;
    }

    importFromDescriptor(matData);
    setSharedMaterial(sharedMaterial);
}


} // namespace ya
