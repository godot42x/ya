#include "ECS/Component/Material/PhongMaterialComponent.h"

#include "Core/Math/Math.h"
#include "Render/Resources/TextureSlotBinding.h"
#include "Render3D/Material/MaterialFactory.h"
#include "Render3D/Material/PhongMaterial.h"
#include "Resource/Core/Model/MaterialData.h"

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

    const auto resource = detail_phong::toTextureResource(resourceEnum);

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
        // semantic default (white, flat normal for normal maps) keeps the
        // material complete and shadable while it waits.
        getMaterial()->setTextureBinding(
            resource, ya::loadingSlotFallback(resourceEnum == EPhongMaterialTextureSlot::Normal));
        return;
    }

    // Failed: an explicit, visible placeholder beats an invisible empty slot.
    YA_CORE_WARN("PhongMaterialComponent: texture '{}' failed to load", slot->textureRef.getPath());
    getMaterial()->setTextureBinding(resource, ya::failedSlotFallback());
    getMaterial()->setTextureParam(
        resource,
        slot->isEnabledEffective(),
        FMath::build_transform_mat3(slot->uvOffset, slot->uvRotation, slot->uvScale));
}

EMaterialResolveResult PhongMaterialComponent::resolve()
{
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

    // 3. Sync every texture slot. A Loading slot binds its semantic default;
    // the slot observer re-queues this component when the fill lands.
    syncTextureSlots();

    _resolveState = EMaterialResolveState::Ready;
    return EMaterialResolveResult::Ready;
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

    _diffuseSlot.textureRef.setPath("");
    _specularSlot.textureRef.setPath("");
    _reflectionSlot.textureRef.setPath("");
    _normalSlot.textureRef.setPath("");

    if (matData.hasTexture(MatTexture::Diffuse)) {
        std::string path = matData.resolveTexturePath(MatTexture::Diffuse);
        _diffuseSlot.textureRef.setPath(path);
    }

    if (matData.hasTexture(MatTexture::Specular)) {
        std::string path = matData.resolveTexturePath(MatTexture::Specular);
        _specularSlot.textureRef.setPath(path);
    }

    if (matData.hasTexture(MatTexture::Normal)) {
        std::string path = matData.resolveTexturePath(MatTexture::Normal);
        _normalSlot.textureRef.setPath(path);
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
