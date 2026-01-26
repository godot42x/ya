#include "PhongMaterialComponent.h"

#include "Core/AssetManager.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/TextureLibrary.h"

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
        std::string matLabel = "PhongMat_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        _material            = MaterialFactory::get()->createMaterial<PhongMaterial>(matLabel);
        _bSharedMaterial     = false; // Created our own material

        if (!_material) {
            YA_CORE_ERROR("PhongMaterialComponent: Failed to create runtime material");
            return false;
        }
    }

    // 2. Sync params to runtime material (direct copy)

    // 3. Resolve texture slots and build texture views
    _material->clearTextureViews();

    for (auto &[key, slot] : _textureSlots) {
        if (slot.textureRef.hasPath() && !slot.isLoaded()) {
            if (!slot.resolve()) {
                YA_CORE_WARN("PhongMaterialComponent: Failed to resolve texture slot {} ({})",
                             getRuntimeMaterial()->getTextureSlotName(key),
                             slot.textureRef.getPath());
                success = false;
                continue;
            }
        }
    }

    syncParams();
    syncTextureSlots();

    _bResolved = true;
    return success;
}

void PhongMaterialComponent::syncParams()
{
    _material->getParamsMut() = _params;
    _material->setParamDirty();
}

void PhongMaterialComponent::syncTextureSlots()
{
    Sampler *defaultSampler = TextureLibrary::get().getDefaultSampler().get();
    for (auto &[key, slot] : _textureSlots) {
        if (slot.isLoaded()) {
            TextureView tv = slot.toTextureView(defaultSampler);
            getRuntimeMaterial()->setTextureView(static_cast<PhongMaterial::EResource>(key), tv);
        }
    }
}


} // namespace ya
