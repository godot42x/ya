#include "LitMaterialComponent.h"

#include "Core/AssetManager.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/TextureLibrary.h"

namespace ya
{

bool LitMaterialComponent::resolve()
{
    if (_bResolved) {
        return true;
    }

    bool success = true;

    // 1. Create runtime material if not exists
    if (!_material) {
        std::string matLabel = "LitMat_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        _material            = MaterialFactory::get()->createMaterial<LitMaterial>(matLabel);

        if (!_material) {
            YA_CORE_ERROR("LitMaterialComponent: Failed to create runtime material");
            return false;
        }
    }

    // 2. Sync params to runtime material (direct copy)

    // 3. Resolve texture slots and build texture views
    _material->clearTextureViews();

    for (auto &[key, slot] : _textureSlots) {
        if (slot.textureRef.hasPath() && !slot.isLoaded()) {
            if (!slot.resolve()) {
                YA_CORE_WARN("LitMaterialComponent: Failed to resolve texture slot {} ({})",
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

void LitMaterialComponent::syncParams()
{
    _material->getParamsMut() = _params;
    _material->setParamDirty();
}

void LitMaterialComponent::syncTextureSlots()
{
    Sampler *defaultSampler = TextureLibrary::get().getDefaultSampler().get();
    for (auto &[key, slot] : _textureSlots) {
        if (slot.isLoaded()) {
            TextureView tv = slot.toTextureView(defaultSampler);
            getRuntimeMaterial()->setTextureView(static_cast<LitMaterial::EResource>(key), tv);
        }
    }
}


} // namespace ya
