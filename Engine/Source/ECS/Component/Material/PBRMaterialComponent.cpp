#include "PBRMaterialComponent.h"

#include "Core/Math/Math.h"

#include <string_view>

namespace ya
{

namespace detail_pbr
{

using TextureResource = PBRMaterial::EResource;

bool containsPathToken(std::string_view propPath, std::string_view token)
{
    return propPath.find(token) != std::string_view::npos;
}

TextureResource textureResourceFromPath(std::string_view propPath)
{
    if (propPath.starts_with("_albedoSlot"))    return TextureResource::AlbedoTexture;
    if (propPath.starts_with("_normalSlot"))    return TextureResource::NormalTexture;
    if (propPath.starts_with("_metallicSlot"))  return TextureResource::MetallicTexture;
    if (propPath.starts_with("_roughnessSlot")) return TextureResource::RoughnessTexture;
    if (propPath.starts_with("_aoSlot"))        return TextureResource::AOTexture;
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

} // namespace detail_pbr

TextureSlot* PBRMaterialComponent::getTextureSlotInternal(PBRMaterial::EResource resourceEnum)
{
    switch (resourceEnum) {
    case PBRMaterial::AlbedoTexture:    return &_albedoSlot;
    case PBRMaterial::NormalTexture:    return &_normalSlot;
    case PBRMaterial::MetallicTexture:  return &_metallicSlot;
    case PBRMaterial::RoughnessTexture: return &_roughnessSlot;
    case PBRMaterial::AOTexture:        return &_aoSlot;
    default: return nullptr;
    }
}

const TextureSlot* PBRMaterialComponent::getTextureSlotInternal(PBRMaterial::EResource resourceEnum) const
{
    switch (resourceEnum) {
    case PBRMaterial::AlbedoTexture:    return &_albedoSlot;
    case PBRMaterial::NormalTexture:    return &_normalSlot;
    case PBRMaterial::MetallicTexture:  return &_metallicSlot;
    case PBRMaterial::RoughnessTexture: return &_roughnessSlot;
    case PBRMaterial::AOTexture:        return &_aoSlot;
    default: return nullptr;
    }
}

PBRMaterialComponent::EditorChangeSummary PBRMaterialComponent::summarizeEditorChanges(const std::vector<std::string>& propPaths)
{
    EditorChangeSummary summary;
    for (const auto& propPath : propPaths) {
        auto resource = detail_pbr::textureResourceFromPath(propPath);
        if (resource == detail_pbr::TextureResource::Count) continue;

        auto index                       = static_cast<size_t>(resource);
        summary.touchedSlots[index]      = true;
        summary.hasTextureSlotChange     = true;
        summary.hasTextureResourceChange = summary.hasTextureResourceChange || detail_pbr::isTextureRefPath(propPath);
    }
    return summary;
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

void PBRMaterialComponent::syncTextureSlot(PBRMaterial::EResource resourceEnum)
{
    if (!getMaterial()) return;

    const TextureSlot* slot = getTextureSlotInternal(resourceEnum);
    if (!slot) return;

    if (slot->isReady()) {
        getMaterial()->setTextureBinding(resourceEnum, slot->toTextureBinding());
    }
    else {
        getMaterial()->clearTextureBinding(resourceEnum);
    }
}

bool PBRMaterialComponent::resolve()
{
    if (_resolveState == EMaterialResolveState::Ready) {
        return true;
    }

    _resolveState = EMaterialResolveState::Resolving;
    bool success = true;

    // 1. Create runtime material if not exists
    if (!_material) {
        createDefaultMaterial();
        if (!_material) {
            YA_CORE_ERROR("PBRMaterialComponent: Failed to create runtime material");
            return false;
        }
    }

    // 2. Sync params
    syncParamsToMaterial();

    // 3. Resolve texture slots
    _material->clearTextureBindings();

    auto resolveSlot = [&](TextureSlot& slot, const char* name) {
        if (slot.hasPath() && !slot.isReady()) {
            if (!slot.resolve()) {
                YA_CORE_WARN("PBRMaterialComponent: Failed to resolve {} texture slot", name);
                success = false;
            }
        }
    };

    resolveSlot(_albedoSlot,    "albedo");
    resolveSlot(_normalSlot,    "normal");
    resolveSlot(_metallicSlot,  "metallic");
    resolveSlot(_roughnessSlot, "roughness");
    resolveSlot(_aoSlot,        "ao");

    syncTextureSlots();

    _resolveState = success ? EMaterialResolveState::Ready : EMaterialResolveState::Failed;
    return success;
}

void PBRMaterialComponent::onEditorPropertyChanged(const std::string& propPath)
{
    onEditorPropertiesChanged({propPath});
}

void PBRMaterialComponent::onEditorPropertiesChanged(const std::vector<std::string>& propPaths)
{
    bool hasParamChange = false;
    for (const auto& propPath : propPaths) {
        hasParamChange = hasParamChange || detail_pbr::isParamPath(propPath);
    }

    const auto summary = summarizeEditorChanges(propPaths);

    if (!summary.hasTextureSlotChange && !hasParamChange) return;

    if (hasParamChange) {
        syncParamsToMaterial();
    }

    if (summary.hasTextureResourceChange) {
        invalidate();
    }

    for (size_t index = 0; index < summary.touchedSlots.size(); ++index) {
        if (!summary.touchedSlots[index]) continue;
        syncTextureSlot(static_cast<PBRMaterial::EResource>(index));
    }
}

void PBRMaterialComponent::syncTextureSlots()
{
    syncTextureSlot(PBRMaterial::EResource::AlbedoTexture);
    syncTextureSlot(PBRMaterial::EResource::NormalTexture);
    syncTextureSlot(PBRMaterial::EResource::MetallicTexture);
    syncTextureSlot(PBRMaterial::EResource::RoughnessTexture);
    syncTextureSlot(PBRMaterial::EResource::AOTexture);
}

void PBRMaterialComponent::importFromDescriptor(const MaterialData& matData)
{
    importParamsFromDescriptor(matData);

    _albedoSlot.textureRef.setPathWithoutNotify("");
    _normalSlot.textureRef.setPathWithoutNotify("");
    _metallicSlot.textureRef.setPathWithoutNotify("");
    _roughnessSlot.textureRef.setPathWithoutNotify("");
    _aoSlot.textureRef.setPathWithoutNotify("");

    if (matData.hasTexture(MatTexture::Diffuse)) {
        _albedoSlot.textureRef.setPathWithoutNotify(matData.resolveTexturePath(MatTexture::Diffuse));
    }
    if (matData.hasTexture(MatTexture::Normal)) {
        _normalSlot.textureRef.setPathWithoutNotify(matData.resolveTexturePath(MatTexture::Normal));
    }
    if (matData.hasTexture(MatTexture::Metallic)) {
        _metallicSlot.textureRef.setPathWithoutNotify(matData.resolveTexturePath(MatTexture::Metallic));
    }
    if (matData.hasTexture(MatTexture::Roughness)) {
        _roughnessSlot.textureRef.setPathWithoutNotify(matData.resolveTexturePath(MatTexture::Roughness));
    }
    if (matData.hasTexture(MatTexture::AO)) {
        _aoSlot.textureRef.setPathWithoutNotify(matData.resolveTexturePath(MatTexture::AO));
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
