#include "ECS/ComponentJson.h"

#include "Core/Reflection/ReflectionSerializer.h"

#include <string>

namespace ya
{

nlohmann::json assembleComponentJson(const void*                        component,
                                     type_index_t                       typeIndex,
                                     std::string_view                   typeName,
                                     const ECSRegistry::IComponentOps*  ops,
                                     ComponentReflectionTrimFn          trimReflection)
{
    nlohmann::json componentJson = nlohmann::json::object();
    if (!ops || ops->useReflectionSerialization(component)) {
        componentJson = ReflectionSerializer::serializeByRuntimeReflection(component, typeIndex, std::string(typeName));
        if (!componentJson.is_object()) {
            componentJson = nlohmann::json::object();
        }
        if (trimReflection) {
            trimReflection(componentJson, typeIndex, typeName);
        }
    }
    if (ops) {
        ops->serializeCustom(component, componentJson);
    }
    return componentJson;
}

} // namespace ya
