#include "class.h"
#include "registry.h"

namespace detail

{

} // namespace detail

Class *Class::getClassByTypeId(refl::type_index_t typeId) const
{
    return ClassRegistry::instance().getClass(typeId);
}

Function &Class::insertFunction(Function function)
{
    const std::string functionName = function.name;
    const auto [it, inserted]      = functions.try_emplace(functionName, std::move(function));
    if (inserted) {
        return it->second;
    }
    if (const auto hook = ClassRegistry::instance().duplicateFunctionHook) {
        hook(*this, functionName);
    }
    // Absorb `.metadata = ...` on the caller's returned reference so a rejected
    // registration cannot patch the function that won.
    static Function discarded;
    discarded = Function{};
    return discarded;
}
