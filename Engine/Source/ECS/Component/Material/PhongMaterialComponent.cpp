#include "PhongMaterialComponent.h"

#include "Core/Math/Math.h"

#include <string_view>


namespace ya
{

namespace detail_phong
{

using TextureResource = PhongMaterial::EResource;

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

TextureSlot* PhongMaterialComponent::getTextureSlotInternal(PhongMaterial::EResource resourceEnum)
{
    switch (resourceEnum) {
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

const TextureSlot* PhongMaterialComponent::getTextureSlotInternal(PhongMaterial::EResource resourceEnum) const
{
    switch (resourceEnum) {
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

PhongMaterialComponent::EditorChangeSummary PhongMaterialComponent::summarizeEditorChanges(const std::vector<std::string>& propPaths)
{
    EditorChangeSummary summary;

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

void PhongMaterialComponent::syncTextureSlot(PhongMaterial::EResource resourceEnum)
{
    if (!getMaterial()) {
        return;
    }

    const TextureSlot* slot = getTextureSlotInternal(resourceEnum);
    if (!slot) {
        return;
    }

    if (slot->isReady()) {
        getMaterial()->setTextureBinding(resourceEnum, slot->toTextureBinding());
        getMaterial()->setTextureParam(
            resourceEnum,
            slot->isEnabledEffective(),
            FMath::build_transform_mat3(slot->uvOffset, slot->uvRotation, slot->uvScale));
    }
    else {
        getMaterial()->clearTextureBinding(resourceEnum);
        getMaterial()->disableTextureParam(resourceEnum);
    }
}

bool PhongMaterialComponent::resolve()
{
    if (_resolveState == EMaterialResolveState::Ready) {
        return true;
    }

    _resolveState = EMaterialResolveState::Resolving;

    bool success = true;

    // 1. Create runtime material if not exists (skip if using shared material)
    if (!_material) {
        createDefaultMaterial();

        if (!_material) {
            YA_CORE_ERROR("PhongMaterialComponent: Failed to create runtime material");
            return false;
        }
    }

    // 2. Sync params to runtime material (component authoring source -> runtime cache)
    syncParamsToMaterial();

    // 3. Resolve texture slots and build texture bindings
    _material->clearTextureBindings();

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
    if (_diffuseSlot.hasPath() && !_diffuseSlot.isReady()) {
        if (!_diffuseSlot.resolve()) {
            YA_CORE_WARN("PhongMaterialComponent: Failed to resolve diffuse texture slot");
            success = false;
        }
    }
    if (_specularSlot.hasPath() && !_specularSlot.isReady()) {
        if (!_specularSlot.resolve()) {
            YA_CORE_WARN("PhongMaterialComponent: Failed to resolve specular texture slot");
            success = false;
        }
    }
    if (_reflectionSlot.hasPath() && !_reflectionSlot.isReady()) {
        if (!_reflectionSlot.resolve()) {
            YA_CORE_WARN("PhongMaterialComponent: Failed to resolve reflection texture slot");
            success = false;
        }
    }
    if (_normalSlot.hasPath() && !_normalSlot.isReady()) {
        if (!_normalSlot.resolve()) {
            YA_CORE_WARN("PhongMaterialComponent: Failed to resolve normal texture slot");
            success = false;
        }
    }

    syncTextureSlots();

    _resolveState = success ? EMaterialResolveState::Ready : EMaterialResolveState::Failed;
    return success;
}

void PhongMaterialComponent::onEditorPropertyChanged(const std::string& propPath)
{
    onEditorPropertiesChanged({propPath});
}

void PhongMaterialComponent::onEditorPropertiesChanged(const std::vector<std::string>& propPaths)
{
    bool hasParamChange = false;
    for (const auto& propPath : propPaths) {
        hasParamChange = hasParamChange || detail_phong::isParamPath(propPath);
    }

    const auto summary = summarizeEditorChanges(propPaths);

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
        syncTextureSlot(static_cast<PhongMaterial::EResource>(index));
    }
}


void PhongMaterialComponent::syncTextureSlots()
{
    syncTextureSlot(PhongMaterial::EResource::DiffuseTexture);
    syncTextureSlot(PhongMaterial::EResource::SpecularTexture);
    syncTextureSlot(PhongMaterial::EResource::ReflectionTexture);
    syncTextureSlot(PhongMaterial::EResource::NormalTexture);
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
