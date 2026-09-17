#include "GameEditor/Inspector/PropertyProjection.h"

#include "GameEditor/Inspector/PropertyGraph.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "Scene3D/TransformComponent.h"

#include <mutex>

namespace ya
{

namespace
{
template <typename TOwner>
void installChangeHooks(PropertyGraph& graph)
{
    const std::vector<void*>& instances = graph.getRootInstances();
    for (PropertyNode& node : graph.getNodesMutable()) {
        const std::string path = node.name;
        node.binding.setChangeHook([instances, path]() {
            for (void* instance : instances) {
                if (instance) {
                    static_cast<TOwner*>(instance)->onPropertyChanged(path);
                }
            }
        });
    }
}
} // namespace

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
                if (PropertyNode* node = graph.find("_position")) {
                    node->displayName = "Position";
                    node->binding.setVec3Setter([](void* object, const glm::vec3& value) {
                        static_cast<TransformComponent*>(object)->setPosition(value);
                    });
                }
                if (PropertyNode* node = graph.find("_rotation")) {
                    node->displayName = "Rotation";
                    node->binding.setVec3Setter([](void* object, const glm::vec3& value) {
                        static_cast<TransformComponent*>(object)->setRotation(value);
                    });
                }
                if (PropertyNode* node = graph.find("_scale")) {
                    node->displayName = "Scale";
                    node->binding.setVec3Setter([](void* object, const glm::vec3& value) {
                        static_cast<TransformComponent*>(object)->setScale(value);
                    });
                }
            });
        PropertyProjectionRegistry::instance().registerProjection(
            type_index_v<PBRMaterialComponent>,
            [](PropertyGraph& graph) {
                installChangeHooks<PBRMaterialComponent>(graph);
            });
        PropertyProjectionRegistry::instance().registerProjection(
            type_index_v<PhongMaterialComponent>,
            [](PropertyGraph& graph) {
                installChangeHooks<PhongMaterialComponent>(graph);
            });
        PropertyProjectionRegistry::instance().registerProjection(
            type_index_v<UnlitMaterialComponent>,
            [](PropertyGraph& graph) {
                installChangeHooks<UnlitMaterialComponent>(graph);
            });
    });
}

} // namespace ya
