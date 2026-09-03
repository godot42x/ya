#include "GameEditor/Inspector/PropertyHandle.h"

#include "Core/Reflection/MetadataSupport.h"
#include "reflects-core/lib.h"

#include <algorithm>
#include <format>

namespace ya
{
namespace
{
bool readManipulateSpec(const Property* property, reflection::Meta::ManipulateSpec& spec)
{
    if (!property || !property->metadata.hasMeta(reflection::Meta::ManipulateSpec::name)) {
        return false;
    }
    try {
        spec = property->metadata.get<reflection::Meta::ManipulateSpec>(reflection::Meta::ManipulateSpec::name);
        return spec.type != reflection::Meta::ManipulateSpec::None;
    }
    catch (...) {
        return false;
    }
}
}

PropertyHandle::PropertyHandle(type_index_t ownerType, std::vector<void*> instances, const Property* property, Vec3Setter setter)
    : _ownerType(ownerType), _instances(std::move(instances)), _property(property), _vec3Setter(std::move(setter))
{
}

bool PropertyHandle::isValid() const
{
    return _property != nullptr && !_instances.empty();
}

bool PropertyHandle::isEditable() const
{
    return isValid() && !_property->bConst && _property->addressGetterMutable &&
           !_property->metadata.hasFlag(FieldFlags::EditReadOnly);
}

bool PropertyHandle::isMixed() const
{
    if (!isValid() || _instances.size() < 2 || !_property->addressGetter) {
        return false;
    }
    const void* first = _property->addressGetter(_instances.front());
    if (!first) return false;
    auto differs = [this, first](void* instance, auto equals) {
        const void* value = _property->addressGetter(instance);
        return !value || !equals(first, value);
    };
    if (_property->typeIndex == refl::type_index_v<glm::vec3>) return std::any_of(_instances.begin() + 1, _instances.end(), [&](void* i) { return differs(i, [](const void* a, const void* b) { return *static_cast<const glm::vec3*>(a) == *static_cast<const glm::vec3*>(b); }); });
    if (_property->typeIndex == refl::type_index_v<float>) return std::any_of(_instances.begin() + 1, _instances.end(), [&](void* i) { return differs(i, [](const void* a, const void* b) { return *static_cast<const float*>(a) == *static_cast<const float*>(b); }); });
    if (_property->typeIndex == refl::type_index_v<bool>) return std::any_of(_instances.begin() + 1, _instances.end(), [&](void* i) { return differs(i, [](const void* a, const void* b) { return *static_cast<const bool*>(a) == *static_cast<const bool*>(b); }); });
    if (_property->typeIndex == refl::type_index_v<std::string>) return std::any_of(_instances.begin() + 1, _instances.end(), [&](void* i) { return differs(i, [](const void* a, const void* b) { return *static_cast<const std::string*>(a) == *static_cast<const std::string*>(b); }); });
    return false;
}

bool PropertyHandle::isMixedVec3Axis(int axis) const
{
    if (axis < 0 || axis > 2 || !isValid() || _instances.size() < 2 ||
        _property->typeIndex != refl::type_index_v<glm::vec3> || !_property->addressGetter) {
        return false;
    }
    const void* firstAddr = _property->addressGetter(_instances.front());
    if (!firstAddr) {
        return false;
    }
    const float first = (*static_cast<const glm::vec3*>(firstAddr))[axis];
    for (size_t i = 1; i < _instances.size(); ++i) {
        const void* address = _property->addressGetter(_instances[i]);
        if (!address || (*static_cast<const glm::vec3*>(address))[axis] != first) {
            return true;
        }
    }
    return false;
}

const std::string& PropertyHandle::getName() const
{
    static const std::string empty;
    return _property ? _property->name : empty;
}

bool PropertyHandle::tryGetVec3(glm::vec3& value) const
{
    if (!isValid() || _property->typeIndex != refl::type_index_v<glm::vec3> || !_property->addressGetter) return false;
    const void* address = _property->addressGetter(_instances.front());
    if (!address) return false;
    value = *static_cast<const glm::vec3*>(address);
    return true;
}

bool PropertyHandle::setVec3(const glm::vec3& value) const
{
    if (!isEditable() || _property->typeIndex != refl::type_index_v<glm::vec3>) return false;
    bool changed = false;
    for (void* instance : _instances) {
        void* address = _property->addressGetterMutable(instance);
        if (!address) continue;
        const auto& current = *static_cast<const glm::vec3*>(address);
        if (current == value) continue;
        if (_vec3Setter) _vec3Setter(instance, value);
        else *static_cast<glm::vec3*>(address) = value;
        changed = true;
    }
    return changed;
}

std::vector<glm::vec3> PropertyHandle::copyVec3() const
{
    std::vector<glm::vec3> values;
    if (!isValid() || _property->typeIndex != refl::type_index_v<glm::vec3> || !_property->addressGetter) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        const void* address = _property->addressGetter(instance);
        if (!address) {
            return {};
        }
        values.push_back(*static_cast<const glm::vec3*>(address));
    }
    return values;
}

bool PropertyHandle::restoreVec3(const std::vector<glm::vec3>& values) const
{
    if (!isEditable() || _property->typeIndex != refl::type_index_v<glm::vec3> ||
        values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        void* address = _property->addressGetterMutable(_instances[i]);
        if (!address) {
            continue;
        }
        if (*static_cast<const glm::vec3*>(address) == values[i]) {
            continue;
        }
        if (_vec3Setter) {
            _vec3Setter(_instances[i], values[i]);
        }
        else {
            *static_cast<glm::vec3*>(address) = values[i];
        }
        changed = true;
    }
    return changed;
}

bool PropertyHandle::tryGetFloat(float& value) const
{
    if (!isValid() || _property->typeIndex != refl::type_index_v<float> || !_property->addressGetter) return false;
    const void* address = _property->addressGetter(_instances.front());
    if (!address) return false;
    value = *static_cast<const float*>(address);
    return true;
}

bool PropertyHandle::setFloat(float value) const
{
    if (!isEditable() || _property->typeIndex != refl::type_index_v<float>) return false;
    bool changed = false;
    for (void* instance : _instances) {
        void* address = _property->addressGetterMutable(instance);
        if (!address) continue;
        auto& current = *static_cast<float*>(address);
        if (current == value) continue;
        current = value;
        changed = true;
    }
    return changed;
}

std::vector<float> PropertyHandle::copyFloat() const
{
    std::vector<float> values;
    if (!isValid() || _property->typeIndex != refl::type_index_v<float> || !_property->addressGetter) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        const void* address = _property->addressGetter(instance);
        if (!address) {
            return {};
        }
        values.push_back(*static_cast<const float*>(address));
    }
    return values;
}

bool PropertyHandle::restoreFloat(const std::vector<float>& values) const
{
    if (!isEditable() || _property->typeIndex != refl::type_index_v<float> || values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        void* address = _property->addressGetterMutable(_instances[i]);
        if (!address) {
            continue;
        }
        auto& current = *static_cast<float*>(address);
        if (current == values[i]) {
            continue;
        }
        current = values[i];
        changed = true;
    }
    return changed;
}

bool PropertyHandle::tryGetBool(bool& value) const
{
    if (!isValid() || _property->typeIndex != refl::type_index_v<bool> || !_property->addressGetter) return false;
    const void* address = _property->addressGetter(_instances.front());
    if (!address) return false;
    value = *static_cast<const bool*>(address);
    return true;
}

bool PropertyHandle::setBool(bool value) const
{
    if (!isEditable() || _property->typeIndex != refl::type_index_v<bool>) return false;
    bool changed = false;
    for (void* instance : _instances) {
        void* address = _property->addressGetterMutable(instance);
        if (!address) continue;
        auto& current = *static_cast<bool*>(address);
        if (current == value) continue;
        current = value;
        changed = true;
    }
    return changed;
}

std::vector<uint8_t> PropertyHandle::copyBool() const
{
    std::vector<uint8_t> values;
    if (!isValid() || _property->typeIndex != refl::type_index_v<bool> || !_property->addressGetter) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        const void* address = _property->addressGetter(instance);
        if (!address) {
            return {};
        }
        values.push_back(*static_cast<const bool*>(address) ? 1 : 0);
    }
    return values;
}

bool PropertyHandle::restoreBool(const std::vector<uint8_t>& values) const
{
    if (!isEditable() || _property->typeIndex != refl::type_index_v<bool> || values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        void* address = _property->addressGetterMutable(_instances[i]);
        if (!address) {
            continue;
        }
        const bool next = values[i] != 0;
        auto& current = *static_cast<bool*>(address);
        if (current == next) {
            continue;
        }
        current = next;
        changed = true;
    }
    return changed;
}

bool PropertyHandle::tryGetString(std::string& value) const
{
    if (!isValid() || _property->typeIndex != refl::type_index_v<std::string> || !_property->addressGetter) return false;
    const void* address = _property->addressGetter(_instances.front());
    if (!address) return false;
    value = *static_cast<const std::string*>(address);
    return true;
}

bool PropertyHandle::setString(const std::string& value) const
{
    if (!isEditable() || _property->typeIndex != refl::type_index_v<std::string>) return false;
    bool changed = false;
    for (void* instance : _instances) {
        void* address = _property->addressGetterMutable(instance);
        if (!address) continue;
        auto& current = *static_cast<std::string*>(address);
        if (current == value) continue;
        current = value;
        changed = true;
    }
    return changed;
}

std::vector<std::string> PropertyHandle::copyString() const
{
    std::vector<std::string> values;
    if (!isValid() || _property->typeIndex != refl::type_index_v<std::string> || !_property->addressGetter) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        const void* address = _property->addressGetter(instance);
        if (!address) {
            return {};
        }
        values.push_back(*static_cast<const std::string*>(address));
    }
    return values;
}

bool PropertyHandle::restoreString(const std::vector<std::string>& values) const
{
    if (!isEditable() || _property->typeIndex != refl::type_index_v<std::string> ||
        values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        void* address = _property->addressGetterMutable(_instances[i]);
        if (!address) {
            continue;
        }
        auto& current = *static_cast<std::string*>(address);
        if (current == values[i]) {
            continue;
        }
        current = values[i];
        changed = true;
    }
    return changed;
}

std::string PropertyHandle::validationError() const
{
    if (!isValid()) {
        return {};
    }
    reflection::Meta::ManipulateSpec spec;
    if (!readManipulateSpec(_property, spec)) {
        return {};
    }
    if (_property->typeIndex == refl::type_index_v<float>) {
        float value = 0.0f;
        if (!tryGetFloat(value)) {
            return "Invalid value";
        }
        if (value < spec.min || value > spec.max) {
            return std::format("Value must be between {} and {}", spec.min, spec.max);
        }
        return {};
    }
    if (_property->typeIndex == refl::type_index_v<glm::vec3>) {
        glm::vec3 value{};
        if (!tryGetVec3(value)) {
            return "Invalid value";
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (value[axis] < spec.min || value[axis] > spec.max) {
                return std::format("Component must be between {} and {}", spec.min, spec.max);
            }
        }
    }
    return {};
}

bool PropertyHandle::tryGetManipulateSpec(reflection::Meta::ManipulateSpec& spec) const
{
    return isValid() && readManipulateSpec(_property, spec);
}

PropertyHandle PropertyHandleFactory::make(type_index_t ownerType, std::vector<void*> instances, std::string_view propertyName, PropertyHandle::Vec3Setter setter)
{
    const Class* cls = ClassRegistry::instance().getClass(ownerType);
    if (!cls) return {};
    auto it = cls->properties.find(std::string(propertyName));
    if (it == cls->properties.end()) return {};
    return PropertyHandle{ownerType, std::move(instances), &it->second, std::move(setter)};
}

} // namespace ya
