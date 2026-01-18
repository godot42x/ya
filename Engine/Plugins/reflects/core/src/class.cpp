#include "class.h"
#include "registry.h"

namespace detail

{

} // namespace detail

Class *Class::getClassByTypeId(refl::type_index_t typeId) const
{
    return ClassRegistry::instance().getClass(typeId);
}
