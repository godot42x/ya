#include "PhongMaterialComponent.h"

#include "Core/AssetManager.h"
#include "Render/Material/MaterialFactory.h"
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

    syncTextureSlots();

    _bResolved = true;
    return success;
}


void PhongMaterialComponent::syncTextureSlots()
{
    Sampler *defaultSampler = TextureLibrary::get().getDefaultSampler();
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
}


} // namespace ya
