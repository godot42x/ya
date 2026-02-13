#include "PhongMaterialComponent.h"

#include "Render/Material/MaterialFactory.h"
#include "Resource/AssetManager.h"
#include "Resource/TextureLibrary.h"


namespace ya
{

bool PhongMaterialComponent::resolve()
{
    if (_bResolved) {
        return true;
    }

    bool success = true;

    // 1. Create runtime material if not exists (skip if using shared material)
    if (!_material) {
        createDefaultMaterial();

        if (!_material) {
            YA_CORE_ERROR("PhongMaterialComponent: Failed to create runtime material");
            return false;
        }
    }

    // 2. Sync params to runtime material (direct copy)

    // 3. Resolve texture slots and build texture views
    _material->clearTextureViews();

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
    if (_diffuseSlot.isValid() && !_diffuseSlot.isLoaded()) {
        if (!_diffuseSlot.resolve()) {
            YA_CORE_WARN("PhongMaterialComponent: Failed to resolve diffuse texture slot");
            success = false;
        }
    }
    if (_specularSlot.isValid() && !_specularSlot.isLoaded()) {
        if (!_specularSlot.resolve()) {
            YA_CORE_WARN("PhongMaterialComponent: Failed to resolve specular texture slot");
            success = false;
        }
    }
    if (_reflectionSlot.isValid() && !_reflectionSlot.isLoaded()) {
        if (!_reflectionSlot.resolve()) {
            YA_CORE_WARN("PhongMaterialComponent: Failed to resolve reflection texture slot");
            success = false;
        }
    }

    syncTextureSlots();

    _bResolved = true;
    return success;
}


void PhongMaterialComponent::syncTextureSlots()
{
    Sampler* defaultSampler = TextureLibrary::get().getDefaultSampler();
    // for (auto &[key, slot] : _textureSlots) {
    //     if (slot.isLoaded()) {
    //         TextureView tv = slot.toTextureView(defaultSampler);
    //         getMaterial()->setTextureView(static_cast<PhongMaterial::EResource>(key), tv);
    //     }
    // }
    if (_diffuseSlot.isLoaded()) {
        TextureView tv = _diffuseSlot.toTextureView(defaultSampler);
        getMaterial()->setTextureView(PhongMaterial::EResource::DiffuseTexture, tv);
    }
    if (_specularSlot.isLoaded()) {
        TextureView tv = _specularSlot.toTextureView(defaultSampler);
        getMaterial()->setTextureView(PhongMaterial::EResource::SpecularTexture, tv);
    }
    if (_reflectionSlot.isLoaded()) {
        TextureView tv = _reflectionSlot.toTextureView(defaultSampler);
        getMaterial()->setTextureView(PhongMaterial::EResource::ReflectionTexture, tv);
    }
}


void PhongMaterialComponent::importFromDescriptor(const MaterialData& matData, bool syncParams)
{
    // 1. Create runtime material if needed
    if (!_material) {
        createDefaultMaterial();
        if (!_material) {
            YA_CORE_ERROR("PhongMaterialComponent::importFromDescriptor: Failed to create runtime material");
            return;
        }
    }

    // 2. Import parameters
    if (syncParams) {
        auto& params     = _material->getParamsMut();
        params.ambient   = matData.getParam<glm::vec3>(MatParam::Ambient, glm::vec3(0.1f));
        params.diffuse   = glm::vec3(matData.getParam<glm::vec4>(MatParam::BaseColor, glm::vec4(1.0f)));
        params.specular  = matData.getParam<glm::vec3>(MatParam::Specular, glm::vec3(0.5f));
        params.shininess = matData.getParam<float>(MatParam::Shininess, 32.0f);
        _material->setParamDirty();
    }

    // 3. Import texture paths
    if (matData.hasTexture(MatTexture::Diffuse)) {
        std::string path = matData.resolveTexturePath(MatTexture::Diffuse);
        _diffuseSlot.textureRef.setPath(path);
    }

    if (matData.hasTexture(MatTexture::Specular)) {
        std::string path = matData.resolveTexturePath(MatTexture::Specular);
        _specularSlot.textureRef.setPath(path);
    }

    if (matData.hasTexture(MatTexture::Normal)) {
        // Note: Normal map would go to a normal slot if available
        // Currently PhongMaterial doesn't have normal map support
        YA_CORE_TRACE("PhongMaterialComponent: Normal map not yet supported");
    }

    // 4. Mark as needing resolve
    invalidate();
}


void PhongMaterialComponent::importFromDescriptorWithSharedMaterial(const MaterialData& matData, PhongMaterial* sharedMaterial)
{
    if (!sharedMaterial) {
        YA_CORE_ERROR("PhongMaterialComponent::importFromDescriptorWithSharedMaterial: sharedMaterial is null");
        return;
    }

    // 1. Use the shared material (component does NOT own it)
    setSharedMaterial(sharedMaterial);

    // 2. Import texture paths to component slots (for serialization and Inspector display)
    //    The actual textures are already loaded in the shared material,
    //    but we need the paths in slots for editing and serialization
    if (matData.hasTexture(MatTexture::Diffuse)) {
        std::string path = matData.resolveTexturePath(MatTexture::Diffuse);
        _diffuseSlot.textureRef.setPathWithoutNotify(path);
    }

    if (matData.hasTexture(MatTexture::Specular)) {
        std::string path = matData.resolveTexturePath(MatTexture::Specular);
        _specularSlot.textureRef.setPathWithoutNotify(path);
    }

    // 3. Mark as resolved (shared material is already fully initialized)
    _bResolved = true;
}


} // namespace ya
