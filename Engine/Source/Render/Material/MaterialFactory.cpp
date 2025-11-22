#include "MaterialFactory.h"

namespace ya
{

namespace detail
{


template <>
MaterialFactory *MaterialFactory::_instance = nullptr;
template <>
void MaterialFactory::init()
{
    YA_CORE_ASSERT(!_instance, "MaterialFactory already initialized!");
    _instance = new MaterialFactory();
    _instance->_materials.clear();
}

template <>
void MaterialFactory::destroy()
{
    _materials.clear();
    delete _instance;
    _instance = nullptr;
}

} // namespace detail

} // namespace ya
