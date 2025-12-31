#include "AutoRegister.h"

namespace ya
{

AutoRegisterRegistry &AutoRegisterRegistry::get()
{
    static AutoRegisterRegistry registry;
    return registry;
}

} // namespace ya
