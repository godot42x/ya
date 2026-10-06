#pragma once

#include "Core/Api.h"
#include "Core/TypeIndex.h"
#include "ECS/ECSRegistry.h"

#include <nlohmann/json.hpp>
#include <string_view>

namespace ya
{

/// Runs on the reflection object only, before custom data is appended.
using ComponentReflectionTrimFn = void (*)(nlohmann::json& reflected, type_index_t typeIndex, std::string_view typeName);

/// The component document shared by scene save and script `component.get`:
/// reflection when the component uses it, then `serializeCustom` appended
/// into that object. `trimReflection` sees only the reflection object.
/// Pass nullptr to keep every reflected field.
[[nodiscard]] YA_ECS_CORE_API nlohmann::json assembleComponentJson(const void*                         component,
                                                                   type_index_t                       typeIndex,
                                                                   std::string_view                   typeName,
                                                                   const ECSRegistry::IComponentOps*  ops,
                                                                   ComponentReflectionTrimFn          trimReflection = nullptr);

} // namespace ya
