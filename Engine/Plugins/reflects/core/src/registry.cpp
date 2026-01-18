#include "registry.h"

ClassRegistry &ClassRegistry::instance()
{
    static ClassRegistry instance;
    return instance;
}

// 全局辅助函数，用于在 class.h 中访问注册表
ClassRegistry& getClassRegistryInstance()
{
    return ClassRegistry::instance();
}

EnumRegistry &EnumRegistry::instance()
{
    static EnumRegistry reg;
    return reg;
}

