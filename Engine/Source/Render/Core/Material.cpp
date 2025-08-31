#include "Material.h"

namespace ya
{


MaterialFactory *MaterialFactory::_instance = nullptr;
void             MaterialFactory::init()
{
    YA_CORE_ASSERT(!_instance, "MaterialFactory already initialized!");
    _instance = new MaterialFactory();
    _instance->_materials.clear();
}

void MaterialFactory::destroy()
{
    _materials.clear();
    delete _instance;
    _instance = nullptr;
}

} // namespace ya
