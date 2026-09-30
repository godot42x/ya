#include "ECS/Component/2D/BillboardComponent.h"

#include "Core/Math/Math.h"
#include "Render3D/Material/MaterialFactory.h"
#include "Render3D/Material/UnlitMaterial.h"
#include "Render/Resources/TextureSlotBinding.h"

namespace ya

{

BillboardComponent::~BillboardComponent()
{
    if (_material) {
        if (auto* factory = MaterialFactory::get()) {
            factory->destroyMaterial(_material);
        }
        _material = nullptr;
    }
}

bool BillboardComponent::resolve()
{
    if (!_material) {
        const std::string label = std::string("Billboard_") + std::to_string(reinterpret_cast<uintptr_t>(this));
        _material               = MaterialFactory::get()->createMaterial<UnlitMaterial>(label);
        if (!_material) {
            YA_CORE_ERROR("BillboardComponent: failed to create runtime material");
            return false;
        }
    }

    auto& params      = _material->getParamsMut();
    params.baseColor0 = glm::vec3(tint);
    params.baseColor1 = glm::vec3(tint);
    params.mixValue   = 0.0f;
    _material->setParamDirty();

    if (!image.hasPath()) {
        _material->clearTextureBinding(UnlitMaterial::BaseColor0);
        _material->disableTextureParam(UnlitMaterial::BaseColor0);
        return true;
    }

    if (image.isReady()) {
        _material->setTextureBinding(UnlitMaterial::BaseColor0, ya::slotToTextureBinding(image));
        _material->setTextureParam(UnlitMaterial::BaseColor0, true, FMath::build_transform_mat3(image.uvOffset, image.uvRotation, image.uvScale));
        return true;
    }

    if (image.isLoading()) {
        // The slot observer re-queues this billboard when the update lands; the
        // semantic default (white) keeps the material complete while it waits.
        _material->setTextureBinding(UnlitMaterial::BaseColor0, ya::loadingSlotFallback(false));
        return true;
    }

    // Failed: an explicit, visible placeholder beats an invisible empty slot.
    YA_CORE_WARN("BillboardComponent: texture '{}' failed to load", image.textureRef.getPath());
    _material->setTextureBinding(UnlitMaterial::BaseColor0, ya::failedSlotFallback());
    _material->setTextureParam(UnlitMaterial::BaseColor0, true, FMath::build_transform_mat3(image.uvOffset, image.uvRotation, image.uvScale));
    return true;
}

} // namespace ya
