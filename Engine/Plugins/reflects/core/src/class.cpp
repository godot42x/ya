#include "class.h"
#include "registry.h"

namespace detail

{

} // namespace detail

Class *Class::getClassByTypeId(refl::type_index_t typeId) const
{
    return ClassRegistry::instance().getClass(typeId);
}

namespace
{

template <typename Member>
void assignMember(std::unordered_map<std::string, Member> &members,
                  std::vector<std::string>               &order,
                  const std::string                      &name,
                  Member                                  value)
{
    if (const auto it = members.find(name); it != members.end()) {
        std::erase(order, name);
        it->second = std::move(value);
    }
    else {
        members.emplace(name, std::move(value));
    }
    order.push_back(name);
}

void accumulateMembers(const Class &cls, const std::vector<refl::type_index_t> &path, ResolvedMembers &out)
{
    for (const refl::type_index_t parent : cls.parents) {
        const Class *parentClass = cls.getClassByTypeId(parent);
        if (parentClass == nullptr) {
            continue;
        }
        std::vector<refl::type_index_t> parentPath = path;
        parentPath.push_back(parent);
        accumulateMembers(*parentClass, parentPath, out);
    }
    for (const auto &[name, property] : cls.properties) {
        assignMember(out.properties, out.propertyOrder, name, ResolvedProperty{&property, path});
    }
    for (const auto &[name, function] : cls.functions) {
        assignMember(out.functions, out.functionOrder, name, ResolvedFunction{&function, path});
    }
}

} // namespace

ResolvedMembers Class::resolveMembers() const
{
    ResolvedMembers out;
    accumulateMembers(*this, {}, out);
    return out;
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
