#include "MaterialFactory.h"

#include "Core/Base.h"

namespace ya
{

namespace detail
{


template <>
MaterialFactory *MaterialFactory::_instance = nullptr;
template <>
YA_RENDER_3D_API MaterialFactory *MaterialFactory::get()
{
    return _instance;
}
template <>
YA_RENDER_3D_API void MaterialFactory::init()
{
    YA_CORE_ASSERT(!_instance, "MaterialFactory already initialized!");
    _instance = new MaterialFactory();
    _instance->_materials.clear();
}

template <>
YA_RENDER_3D_API void MaterialFactory::destroy()
{
    _materials.clear();
    delete _instance;
    _instance = nullptr;
}

} // namespace detail

} // namespace ya
