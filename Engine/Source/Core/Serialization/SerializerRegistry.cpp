#include "SerializerRegistry.h"

namespace ya

{

ECSSerializerRegistry &ECSSerializerRegistry::get()
{
    static ECSSerializerRegistry registry;
    return registry;
}

} // namespace ya
