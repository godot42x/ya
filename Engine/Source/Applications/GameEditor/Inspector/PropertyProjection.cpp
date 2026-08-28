#include "GameEditor/Inspector/PropertyProjection.h"

#include "GameEditor/Inspector/PropertyGraph.h"
#include "Scene3D/TransformComponent.h"

#include <mutex>

namespace ya
{

PropertyProjectionRegistry& PropertyProjectionRegistry::instance()
{
    static PropertyProjectionRegistry registry;
    return registry;
}

void PropertyProjectionRegistry::registerProjection(type_index_t ownerType, PropertyProjection projection)
{
    _projections[ownerType] = std::move(projection);
}

void PropertyProjectionRegistry::apply(type_index_t ownerType, PropertyGraph& graph) const
{
    auto it = _projections.find(ownerType);
    if (it != _projections.end()) it->second(graph);
}

void registerBuiltinPropertyProjections()
{
    static std::once_flag once;
    std::call_once(once, [] {
        PropertyProjectionRegistry::instance().registerProjection(
            type_index_v<TransformComponent>,
            [](PropertyGraph& graph) {
                if (PropertyNode* node = graph.find("_position")) node->displayName = "Position";
                if (PropertyNode* node = graph.find("_rotation")) node->displayName = "Rotation";
                if (PropertyNode* node = graph.find("_scale")) node->displayName = "Scale";
            });
    });
}

} // namespace ya
