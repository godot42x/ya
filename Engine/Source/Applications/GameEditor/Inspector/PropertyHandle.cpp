#include "GameEditor/Inspector/PropertyHandle.h"

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/PropertyAccessor.h"
#include "reflects-core/lib.h"

#include <algorithm>

namespace ya
{
namespace
{

using reflection::PropertyAccessor;

void notifyChanged(const PropertyHandle::ChangeHook& hook, bool changed)
{
    if (changed && hook) {
        hook();
    }
}

bool anyChanged(bool lhs, bool rhs)
{
    return lhs || rhs;
}

} // namespace

PropertyHandle::PropertyHandle(type_index_t ownerType, std::vector<void*> instances, const Property* property, Vec3Setter setter, int elementIndex)
    : _ownerType(ownerType), _instances(std::move(instances)), _property(property), _elementIndex(elementIndex), _vec3Setter(std::move(setter))
{
}

bool PropertyHandle::isValid() const
{
    return _property != nullptr && !_instances.empty();
}

bool PropertyHandle::isEditable() const
{
    return isValid() && PropertyAccessor::isEditable(*_property);
}

bool PropertyHandle::isEnum() const
{
    return isValid() && PropertyAccessor::isEnum(*_property, _elementIndex);
}

bool PropertyHandle::isColor() const
{
    return isValid() && PropertyAccessor::isColor(*_property);
}

bool PropertyHandle::isAssetRef() const
{
    return isValid() && PropertyAccessor::isAssetRefType(PropertyAccessor::valueType(*_property, _elementIndex));
}

std::optional<EEditorAssetPickerKind> PropertyHandle::assetRefKind() const
{
    if (!isAssetRef()) {
        return std::nullopt;
    }
    if (PropertyAccessor::valueType(*_property, _elementIndex) == refl::type_index_v<TextureRef>) {
        return EEditorAssetPickerKind::Texture;
    }
    if (PropertyAccessor::valueType(*_property, _elementIndex) == refl::type_index_v<ModelRef>) {
        return EEditorAssetPickerKind::Model;
    }
    if (PropertyAccessor::valueType(*_property, _elementIndex) == refl::type_index_v<MeshRef>) {
        return EEditorAssetPickerKind::Mesh;
    }
    return std::nullopt;
}

bool PropertyHandle::isMixed() const
{
    if (!isValid() || _instances.size() < 2) {
        return false;
    }
    const void* first = PropertyAccessor::address(*_property, _instances.front(), _elementIndex);
    if (!first) {
        return false;
    }
    return std::any_of(_instances.begin() + 1, _instances.end(), [&](void* instance) {
        const void* address = PropertyAccessor::address(*_property, instance, _elementIndex);
        return !address || !PropertyAccessor::equals(*_property, first, address, _elementIndex);
    });
}

bool PropertyHandle::isMixedVecAxis(int axis, int componentCount) const
{
    if (!isValid() || _instances.size() < 2) {
        return false;
    }
    const void* first = PropertyAccessor::address(*_property, _instances.front(), _elementIndex);
    if (!first) {
        return false;
    }
    return std::any_of(_instances.begin() + 1, _instances.end(), [&](void* instance) {
        const void* address = PropertyAccessor::address(*_property, instance, _elementIndex);
        return !address || !PropertyAccessor::equalsVecAxis(*_property, first, address, axis, componentCount, _elementIndex);
    });
}

bool PropertyHandle::isMixedVec3Axis(int axis) const
{
    return isMixedVecAxis(axis, 3);
}

const std::string& PropertyHandle::getName() const
{
    static const std::string empty;
    return _property ? _property->name : empty;
}

bool PropertyHandle::tryGetVec2(glm::vec2& value) const
{
    return isValid() && PropertyAccessor::tryGetVec2(*_property, _instances.front(), value, _elementIndex);
}

bool PropertyHandle::setVec2(const glm::vec2& value) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setVec2(*_property, instance, value, _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

std::vector<glm::vec2> PropertyHandle::copyVec2() const
{
    std::vector<glm::vec2> values;
    if (!isValid()) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        glm::vec2 value{};
        if (!PropertyAccessor::tryGetVec2(*_property, instance, value, _elementIndex)) {
            return {};
        }
        values.push_back(value);
    }
    return values;
}

bool PropertyHandle::restoreVec2(const std::vector<glm::vec2>& values) const
{
    if (!isEditable() || values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        changed = anyChanged(changed, PropertyAccessor::setVec2(*_property, _instances[i], values[i], _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

bool PropertyHandle::tryGetVec3(glm::vec3& value) const
{
    return isValid() && PropertyAccessor::tryGetVec3(*_property, _instances.front(), value, _elementIndex);
}

bool PropertyHandle::setVec3(const glm::vec3& value) const
{
    if (!isEditable() || PropertyAccessor::valueType(*_property, _elementIndex) != refl::type_index_v<glm::vec3>) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        glm::vec3 current{};
        if (!PropertyAccessor::tryGetVec3(*_property, instance, current, _elementIndex) || current == value) {
            continue;
        }
        if (_vec3Setter) {
            _vec3Setter(instance, value);
        }
        else if (!PropertyAccessor::setVec3(*_property, instance, value, _elementIndex)) {
            continue;
        }
        changed = true;
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

std::vector<glm::vec3> PropertyHandle::copyVec3() const
{
    std::vector<glm::vec3> values;
    if (!isValid()) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        glm::vec3 value{};
        if (!PropertyAccessor::tryGetVec3(*_property, instance, value, _elementIndex)) {
            return {};
        }
        values.push_back(value);
    }
    return values;
}

bool PropertyHandle::restoreVec3(const std::vector<glm::vec3>& values) const
{
    if (!isEditable() || values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        glm::vec3 current{};
        if (!PropertyAccessor::tryGetVec3(*_property, _instances[i], current, _elementIndex) || current == values[i]) {
            continue;
        }
        if (_vec3Setter) {
            _vec3Setter(_instances[i], values[i]);
        }
        else if (!PropertyAccessor::setVec3(*_property, _instances[i], values[i], _elementIndex)) {
            continue;
        }
        changed = true;
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

bool PropertyHandle::tryGetVec4(glm::vec4& value) const
{
    return isValid() && PropertyAccessor::tryGetVec4(*_property, _instances.front(), value, _elementIndex);
}

bool PropertyHandle::setVec4(const glm::vec4& value) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setVec4(*_property, instance, value, _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

std::vector<glm::vec4> PropertyHandle::copyVec4() const
{
    std::vector<glm::vec4> values;
    if (!isValid()) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        glm::vec4 value{};
        if (!PropertyAccessor::tryGetVec4(*_property, instance, value, _elementIndex)) {
            return {};
        }
        values.push_back(value);
    }
    return values;
}

bool PropertyHandle::restoreVec4(const std::vector<glm::vec4>& values) const
{
    if (!isEditable() || values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        changed = anyChanged(changed, PropertyAccessor::setVec4(*_property, _instances[i], values[i], _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

bool PropertyHandle::tryGetFloat(float& value) const
{
    return isValid() && PropertyAccessor::tryGetFloat(*_property, _instances.front(), value, _elementIndex);
}

bool PropertyHandle::setFloat(float value) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setFloat(*_property, instance, value, _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

std::vector<float> PropertyHandle::copyFloat() const
{
    std::vector<float> values;
    if (!isValid()) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        float value = 0.0f;
        if (!PropertyAccessor::tryGetFloat(*_property, instance, value, _elementIndex)) {
            return {};
        }
        values.push_back(value);
    }
    return values;
}

bool PropertyHandle::restoreFloat(const std::vector<float>& values) const
{
    if (!isEditable() || values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        changed = anyChanged(changed, PropertyAccessor::setFloat(*_property, _instances[i], values[i], _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

bool PropertyHandle::tryGetInteger(int64_t& value) const
{
    return isValid() && PropertyAccessor::tryGetInteger(*_property, _instances.front(), value, _elementIndex);
}

bool PropertyHandle::setInteger(int64_t value) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setInteger(*_property, instance, value, _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

std::vector<int64_t> PropertyHandle::copyInteger() const
{
    std::vector<int64_t> values;
    if (!isValid()) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        int64_t value = 0;
        if (!PropertyAccessor::tryGetInteger(*_property, instance, value, _elementIndex)) {
            return {};
        }
        values.push_back(value);
    }
    return values;
}

bool PropertyHandle::restoreInteger(const std::vector<int64_t>& values) const
{
    if (!isEditable() || values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        changed = anyChanged(changed, PropertyAccessor::setInteger(*_property, _instances[i], values[i], _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

bool PropertyHandle::tryGetBool(bool& value) const
{
    return isValid() && PropertyAccessor::tryGetBool(*_property, _instances.front(), value, _elementIndex);
}

bool PropertyHandle::setBool(bool value) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setBool(*_property, instance, value, _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

std::vector<uint8_t> PropertyHandle::copyBool() const
{
    std::vector<uint8_t> values;
    if (!isValid()) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        bool value = false;
        if (!PropertyAccessor::tryGetBool(*_property, instance, value, _elementIndex)) {
            return {};
        }
        values.push_back(value ? 1 : 0);
    }
    return values;
}

bool PropertyHandle::restoreBool(const std::vector<uint8_t>& values) const
{
    if (!isEditable() || values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        changed = anyChanged(changed, PropertyAccessor::setBool(*_property, _instances[i], values[i] != 0, _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

bool PropertyHandle::tryGetString(std::string& value) const
{
    return isValid() && PropertyAccessor::tryGetString(*_property, _instances.front(), value, _elementIndex);
}

bool PropertyHandle::setString(const std::string& value) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setString(*_property, instance, value, _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

std::vector<std::string> PropertyHandle::copyString() const
{
    std::vector<std::string> values;
    if (!isValid()) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        std::string value;
        if (!PropertyAccessor::tryGetString(*_property, instance, value, _elementIndex)) {
            return {};
        }
        values.push_back(std::move(value));
    }
    return values;
}

bool PropertyHandle::restoreString(const std::vector<std::string>& values) const
{
    if (!isEditable() || values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        changed = anyChanged(changed, PropertyAccessor::setString(*_property, _instances[i], values[i], _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

bool PropertyHandle::tryGetEnumIndex(int& index) const
{
    return isValid() && PropertyAccessor::tryGetEnumIndex(*_property, _instances.front(), index, _elementIndex);
}

bool PropertyHandle::enumLabels(std::vector<std::string>& labels) const
{
    return isValid() && PropertyAccessor::enumLabels(*_property, labels, _elementIndex);
}

bool PropertyHandle::setEnumByIndex(int index) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setEnumByIndex(*_property, instance, index, _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

std::vector<int64_t> PropertyHandle::copyEnum() const
{
    std::vector<int64_t> values;
    if (!isValid()) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        int64_t value = 0;
        if (!PropertyAccessor::tryGetEnumValue(*_property, instance, value, _elementIndex)) {
            return {};
        }
        values.push_back(value);
    }
    return values;
}

bool PropertyHandle::restoreEnum(const std::vector<int64_t>& values) const
{
    if (!isEditable() || values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        changed = anyChanged(changed, PropertyAccessor::setEnumValue(*_property, _instances[i], values[i], _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

bool PropertyHandle::tryGetColor(glm::vec4& value) const
{
    return isValid() && PropertyAccessor::tryGetColor(*_property, _instances.front(), value, _elementIndex);
}

bool PropertyHandle::setColor(const glm::vec4& value) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setColor(*_property, instance, value, _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

std::vector<glm::vec4> PropertyHandle::copyColor() const
{
    std::vector<glm::vec4> values;
    if (!isValid()) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        glm::vec4 value{};
        if (!PropertyAccessor::tryGetColor(*_property, instance, value, _elementIndex)) {
            return {};
        }
        values.push_back(value);
    }
    return values;
}

bool PropertyHandle::restoreColor(const std::vector<glm::vec4>& values) const
{
    if (!isEditable() || values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        changed = anyChanged(changed, PropertyAccessor::setColor(*_property, _instances[i], values[i], _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

bool PropertyHandle::tryGetAssetPath(std::string& value) const
{
    return isValid() && PropertyAccessor::tryGetAssetPath(*_property, _instances.front(), value, _elementIndex);
}

bool PropertyHandle::setAssetPath(const std::string& value) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setAssetPath(*_property, instance, value, _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

std::vector<std::string> PropertyHandle::copyAssetPath() const
{
    std::vector<std::string> values;
    if (!isValid()) {
        return values;
    }
    values.reserve(_instances.size());
    for (void* instance : _instances) {
        std::string value;
        if (!PropertyAccessor::tryGetAssetPath(*_property, instance, value, _elementIndex)) {
            return {};
        }
        values.push_back(std::move(value));
    }
    return values;
}

bool PropertyHandle::restoreAssetPath(const std::vector<std::string>& values) const
{
    if (!isEditable() || values.size() != _instances.size()) {
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < _instances.size(); ++i) {
        changed = anyChanged(changed, PropertyAccessor::setAssetPath(*_property, _instances[i], values[i], _elementIndex));
    }
    notifyChanged(_changeHook, changed);
    return changed;
}

bool PropertyHandle::hasAssetResolveError() const
{
    if (!isAssetRef()) {
        return false;
    }
    return std::any_of(_instances.begin(), _instances.end(), [&](void* instance) {
        return PropertyAccessor::hasAssetResolveError(*_property, instance, _elementIndex);
    });
}

std::string PropertyHandle::validationError() const
{
    if (!isValid()) {
        return {};
    }
    return PropertyAccessor::validationError(*_property, _instances.front(), _elementIndex);
}

bool PropertyHandle::tryGetManipulateSpec(reflection::Meta::ManipulateSpec& spec) const
{
    return isValid() && PropertyAccessor::tryGetManipulateSpec(*_property, spec);
}

PropertyHandle PropertyHandleFactory::make(type_index_t ownerType, std::vector<void*> instances, std::string_view propertyName, PropertyHandle::Vec3Setter setter)
{
    const Class* cls = ClassRegistry::instance().getClass(ownerType);
    if (!cls) {
        return {};
    }
    auto it = cls->properties.find(std::string(propertyName));
    if (it == cls->properties.end()) {
        return {};
    }
    return PropertyHandle{ownerType, std::move(instances), &it->second, std::move(setter)};
}

} // namespace ya
