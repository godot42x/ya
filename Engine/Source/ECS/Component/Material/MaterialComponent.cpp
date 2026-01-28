#include "MaterialComponent.h"
#include "Render/Material/MaterialFactory.h"

namespace ya
{

template <typename MaterialType>
MaterialType *MaterialComponent<MaterialType>::createDefaultMaterial()
{
    // if(_material)
    std::string matLabel = typeid(MaterialType).name() + std::to_string(reinterpret_cast<uintptr_t>(this));
    _material            = MaterialFactory::get()->createMaterial<MaterialType>(matLabel);
    _bSharedMaterial     = false; // Created our own material
    return _material;
}

} // namespace ya
