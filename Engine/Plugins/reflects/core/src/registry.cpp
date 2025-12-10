#include "registry.h"

ClassRegistry &ClassRegistry::instance()
{
    static ClassRegistry instance;
    return instance;
}

EnumRegistry &EnumRegistry::instance()
{
    static EnumRegistry reg;
    return reg;
}
