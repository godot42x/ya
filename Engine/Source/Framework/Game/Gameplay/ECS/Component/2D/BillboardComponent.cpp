#include "BillboardComponent.h"

#include "Core/Math/Math.h"
#include "Render3D/Material/MaterialFactory.h"
#include "GUI/Runtime/Resource/TextureLibrary.h"

namespace ya

{

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
        bDirty = false;
        return true;
    }

    if (image.isReady()) {
        _material->setTextureBinding(UnlitMaterial::BaseColor0, image.toTextureBinding());
        _material->setTextureParam(UnlitMaterial::BaseColor0, true, FMath::build_transform_mat3(image.uvOffset, image.uvRotation, image.uvScale));
        bDirty = false;
        return true;
    }

    const auto result = image.resolve();
    if (result == EAssetResolveResult::Ready) {
        _material->setTextureBinding(UnlitMaterial::BaseColor0, image.toTextureBinding());
        _material->setTextureParam(UnlitMaterial::BaseColor0, true, FMath::build_transform_mat3(image.uvOffset, image.uvRotation, image.uvScale));
        bDirty = false;
        return true;
    }

    if (result == EAssetResolveResult::Pending) {
        bDirty = true;
        return false;
    }

    _material->clearTextureBinding(UnlitMaterial::BaseColor0);
    _material->disableTextureParam(UnlitMaterial::BaseColor0);
    YA_CORE_WARN("Billboard texture resolve failed");
    bDirty = false;
    return false;
}

} // namespace ya
