#include "ECS/ComponentJson.h"

#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/Reflection/ValueFacets.h"

#include <string>

namespace ya
{
namespace
{

const reflection::FComponentJsonFacet* componentJsonFacet(type_index_t typeIndex)
{
    return refl::TypeRegistry::instance().facet<reflection::FComponentJsonFacet>(typeIndex);
}

} // namespace

bool componentUsesReflection(type_index_t typeIndex)
{
    const reflection::FComponentJsonFacet* facet = componentJsonFacet(typeIndex);
    return facet == nullptr || facet->bUseReflection;
}

nlohmann::json assembleComponentJson(const void*                component,
                                     type_index_t              typeIndex,
                                     std::string_view          typeName,
                                     ComponentReflectionTrimFn trimReflection)
{
    const reflection::FComponentJsonFacet* facet = componentJsonFacet(typeIndex);
    nlohmann::json componentJson = nlohmann::json::object();
    if (facet == nullptr || facet->bUseReflection) {
        componentJson = ReflectionSerializer::serializeByRuntimeReflection(component, typeIndex, std::string(typeName));
        if (!componentJson.is_object()) {
            componentJson = nlohmann::json::object();
        }
        if (trimReflection) {
            trimReflection(componentJson, typeIndex, typeName);
        }
    }
    if (facet != nullptr && facet->serialize != nullptr) {
        facet->serialize(component, componentJson);
    }
    return componentJson;
}

void readComponentJson(void*                 component,
                       type_index_t          typeIndex,
                       const nlohmann::json& json,
                       std::string_view      typeName)
{
    const reflection::FComponentJsonFacet* facet = componentJsonFacet(typeIndex);
    if (facet == nullptr || facet->bUseReflection) {
        if (ClassRegistry::instance().getClass(typeIndex) != nullptr) {
            ReflectionSerializer::deserializeByRuntimeReflection(component, typeIndex, json, std::string(typeName));
        }
    }
    if (facet != nullptr && facet->deserialize != nullptr) {
        facet->deserialize(component, json);
    }
}

} // namespace ya
