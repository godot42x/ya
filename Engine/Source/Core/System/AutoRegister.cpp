#include "AutoRegister.h"
#include "TypeRegistry.h"

namespace ya
{

// ============================================================================
// 辅助函数：将反射注册转发到 TypeRegistry
// ============================================================================

void registerReflectionToTypeRegistry(std::function<void()> registrar)
{
    TypeRegistry::get()->addReflectionRegistrar(std::move(registrar));
}

} // namespace ya
