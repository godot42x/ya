#pragma once

#include "Core/Base.h"
#include "Core/FName.h"
#include <entt/entt.hpp>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

namespace ya
{
struct ECSRegistry
{

  public:

    static ECSRegistry &get();


    using component_getter  = std::function<void *(entt::registry &, entt::entity)>;
    using component_creator = std::function<void *(entt::registry &, entt::entity)>;

    std::unordered_map<FName, uint32_t>             _typeIndexCache;
    std::unordered_map<uint32_t, component_getter>  _componentGetters;
    std::unordered_map<uint32_t, component_creator> _componentCreators;

    template <typename T>
    void registerComponent(const std::string &name, auto &&componentGetter, auto &&componentCreator)
    {
        uint32_t typeIndex = ya::type_index_v<T>;

        _componentGetters[typeIndex]  = componentGetter;
        _componentCreators[typeIndex] = componentCreator;
        _typeIndexCache[FName(name)]  = typeIndex;
    }
};


#if 1
namespace
{
void test()
{
    ECSRegistry::get().registerComponent<ECSRegistry>(
        "TestComponent",
        [](entt::registry &reg, entt::entity entity) {
            return (void *)&reg.get<ECSRegistry>(entity);
        },
        [](entt::registry &reg, entt::entity entity) {
            return &reg.emplace<ECSRegistry>(entity);
        });
}
} // namespace
#endif

} // namespace ya
