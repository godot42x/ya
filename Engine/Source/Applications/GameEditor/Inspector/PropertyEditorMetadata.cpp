#include "GameEditor/Inspector/PropertyEditorMetadata.h"

#include "reflects-core/lib.h"

#include <format>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace ya
{
namespace
{

bool readManipulateSpec(const Property& property, reflection::Meta::ManipulateSpec& spec)
{
    if (!property.metadata.hasMeta(reflection::Meta::ManipulateSpec::name)) {
        return false;
    }
    try {
        spec = property.metadata.get<reflection::Meta::ManipulateSpec>(reflection::Meta::ManipulateSpec::name);
        return spec.type != reflection::Meta::ManipulateSpec::None;
    }
    catch (...) {
        return false;
    }
}

} // namespace

bool PropertyEditorMetadata::isColor(const Property& property)
{
    if (!property.metadata.hasMeta(reflection::Meta::Color)) {
        return false;
    }
    try {
        return property.metadata.get<bool>(reflection::Meta::Color);
    }
    catch (...) {
        return false;
    }
}

bool PropertyEditorMetadata::tryGetManipulateSpec(const Property& property,
                                                  reflection::Meta::ManipulateSpec& spec)
{
    return readManipulateSpec(property, spec);
}

std::string PropertyEditorMetadata::validationError(const reflection::FPropertySlot& slot,
                                                    const void* instance)
{
    if (!slot.property) {
        return {};
    }
    reflection::Meta::ManipulateSpec spec;
    if (!readManipulateSpec(*slot.property, spec)) {
        return {};
    }
    const auto outOfRange = [&](double value) { return value < spec.min || value > spec.max; };
    const type_index_t type = reflection::PropertyAccessor::valueType(slot);
    if (type == refl::type_index_v<float>) {
        float value = 0.0f;
        if (!reflection::PropertyAccessor::tryGet(slot, instance, value)) {
            return "Invalid value";
        }
        return outOfRange(value) ? std::format("Value must be between {} and {}", spec.min, spec.max) : std::string{};
    }
    if (reflection::PropertyAccessor::isIntegerType(type)) {
        int64_t value = 0;
        if (!reflection::PropertyAccessor::tryGetInteger(slot, instance, value)) {
            return "Invalid value";
        }
        return outOfRange(static_cast<double>(value))
            ? std::format("Value must be between {} and {}", spec.min, spec.max)
            : std::string{};
    }
    const auto validateVector = [&](auto tag, int count) -> std::string {
        using VectorType = decltype(tag);
        VectorType value{};
        if (!reflection::PropertyAccessor::tryGet(slot, instance, value)) {
            return "Invalid value";
        }
        for (int axis = 0; axis < count; ++axis) {
            if (outOfRange(value[axis])) {
                return std::format("Component must be between {} and {}", spec.min, spec.max);
            }
        }
        return {};
    };
    if (type == refl::type_index_v<glm::vec2>) {
        return validateVector(glm::vec2{}, 2);
    }
    if (type == refl::type_index_v<glm::vec3>) {
        return validateVector(glm::vec3{}, 3);
    }
    if (type == refl::type_index_v<glm::vec4>) {
        return validateVector(glm::vec4{}, 4);
    }
    return {};
}

} // namespace ya
