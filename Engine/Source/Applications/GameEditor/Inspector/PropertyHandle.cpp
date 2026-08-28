#include "GameEditor/Inspector/PropertyHandle.h"

#include "reflects-core/lib.h"

#include <algorithm>

namespace ya
{

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

PropertyHandle PropertyHandleFactory::make(type_index_t ownerType, std::vector<void*> instances, std::string_view propertyName, PropertyHandle::Vec3Setter setter)
{
    const Class* cls = ClassRegistry::instance().getClass(ownerType);
    if (!cls) return {};
    auto it = cls->properties.find(std::string(propertyName));
    if (it == cls->properties.end()) return {};
    return PropertyHandle{ownerType, std::move(instances), &it->second, std::move(setter)};
}

} // namespace ya
