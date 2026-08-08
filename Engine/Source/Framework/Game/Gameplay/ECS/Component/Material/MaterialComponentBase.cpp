#include "MaterialComponentBase.h"

#include "Render3D/Material/MaterialFactory.h"

namespace ya
{

MaterialComponentBase::~MaterialComponentBase()
{
    releaseMaterial();
}

void MaterialComponentBase::releaseMaterial()
{
    if (_material && !_bSharedMaterial) {
        if (auto* factory = MaterialFactory::get()) {
            factory->destroyMaterial(_material);
        }
    }
    _material        = nullptr;
    _bSharedMaterial = false;
}

void MaterialComponentBase::setSharedMaterialBase(Material* material)
{
    if (_material == material && _bSharedMaterial) {
        return;
    }

    releaseMaterial();
    _material        = material;
    _bSharedMaterial = (_material != nullptr);
}

} // namespace ya
