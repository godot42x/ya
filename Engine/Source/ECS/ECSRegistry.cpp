#include "ECSRegistry.h"

namespace ya

{

ECSRegistry &ECSRegistry::get()
{
    static ECSRegistry instance;
    return instance;
}

} // namespace ya
