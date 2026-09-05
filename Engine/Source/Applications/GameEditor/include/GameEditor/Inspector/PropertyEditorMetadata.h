#pragma once

#include "Core/Reflection/MetadataSupport.h"
#include "Core/Reflection/PropertyAccessor.h"

#include <glm/vec4.hpp>
#include <string>

namespace ya
{

/// Editor-only interpretation of reflected metadata and validation rules.
/// Core Reflection remains responsible for locating and reading values.
class PropertyEditorMetadata final
{
  public:
    [[nodiscard]] static bool isColor(const Property& property);
    [[nodiscard]] static bool tryGetManipulateSpec(const Property& property,
                                                    reflection::Meta::ManipulateSpec& spec);
    [[nodiscard]] static std::string validationError(const reflection::FPropertySlot& slot,
                                                     const void* instance);
};

} // namespace ya
