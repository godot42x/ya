#pragma once

#include "Core/Api.h"
#include "Core/TypeIndex.h"

#include <nlohmann/json.hpp>
#include <string_view>

namespace ya
{

/// Runs on the reflection object only, before custom data is appended.
using ComponentReflectionTrimFn = void (*)(nlohmann::json& reflected, type_index_t typeIndex, std::string_view typeName);

/// The component document shared by scene save and script `component.get`:
/// reflection when the component's JSON facet uses it, then the facet's
/// `serialize` appended into that object. `trimReflection` sees only the
/// reflection object. Pass nullptr to keep every reflected field.
[[nodiscard]] YA_ECS_CORE_API nlohmann::json assembleComponentJson(const void*                component,
                                                                   type_index_t              typeIndex,
                                                                   std::string_view          typeName,
                                                                   ComponentReflectionTrimFn trimReflection = nullptr);

/// True when the component document includes the reflected fields.
[[nodiscard]] YA_ECS_CORE_API bool componentUsesReflection(type_index_t typeIndex);

/// Writes a component document the way a scene loads it: reflection when the
/// facet says so, then the facet's `deserialize`.
YA_ECS_CORE_API void readComponentJson(void*                  component,
                                        type_index_t           typeIndex,
                                        const nlohmann::json&  json,
                                        std::string_view       typeName);

} // namespace ya
