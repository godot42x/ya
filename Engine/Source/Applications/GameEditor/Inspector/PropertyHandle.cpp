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

bool anyChanged(bool lhs, bool rhs)
{
    return lhs || rhs;
}

} // namespace

PropertyHandle::PropertyHandle(type_index_t ownerType, std::vector<void*> instances, reflection::FPropertySlot slot)
    : _ownerType(ownerType), _instances(std::move(instances)), _slot(std::move(slot))
{
}

bool PropertyHandle::isValid() const
{
    return _slot.property != nullptr && !_instances.empty();
}

bool PropertyHandle::isEditable() const
{
    return isValid() && PropertyAccessor::isEditable(*_slot.property);
}

bool PropertyHandle::isEnum() const
{
    return isValid() && PropertyAccessor::isEnum(_slot);
}

bool PropertyHandle::isColor() const
{
    return isValid() && PropertyAccessor::isColor(*_slot.property);
}

bool PropertyHandle::isAssetRef() const
{
    return isValid() && PropertyAccessor::isAssetRefType(PropertyAccessor::valueType(_slot));
}

std::optional<EEditorAssetPickerKind> PropertyHandle::assetRefKind() const
{
    if (!isAssetRef()) {
        return std::nullopt;
    }
    const type_index_t type = PropertyAccessor::valueType(_slot);
    if (type == refl::type_index_v<TextureRef>) {
        return EEditorAssetPickerKind::Texture;
    }
    if (type == refl::type_index_v<ModelRef>) {
        return EEditorAssetPickerKind::Model;
    }
    if (type == refl::type_index_v<MeshRef>) {
        return EEditorAssetPickerKind::Mesh;
    }
    return std::nullopt;
}

bool PropertyHandle::isMixed() const
{
    if (!isValid() || _instances.size() < 2) {
        return false;
    }
    const void* first = PropertyAccessor::address(_slot, _instances.front());
    if (!first) {
        return false;
    }
    return std::any_of(_instances.begin() + 1, _instances.end(), [&](void* instance) {
        const void* address = PropertyAccessor::address(_slot, instance);
        return !address || !PropertyAccessor::equals(_slot, first, address);
    });
}

bool PropertyHandle::isMixedVecAxis(int axis, int componentCount) const
{
    if (!isValid() || _instances.size() < 2) {
        return false;
    }
    const void* first = PropertyAccessor::address(_slot, _instances.front());
    if (!first) {
        return false;
    }
    return std::any_of(_instances.begin() + 1, _instances.end(), [&](void* instance) {
        const void* address = PropertyAccessor::address(_slot, instance);
        return !address || !PropertyAccessor::equalsVecAxis(_slot, first, address, axis, componentCount);
    });
}

bool PropertyHandle::isMixedVec3Axis(int axis) const
{
    return isMixedVecAxis(axis, 3);
}

const std::string& PropertyHandle::getName() const
{
    static const std::string empty;
    return _slot.property ? _slot.property->name : empty;
}

bool PropertyHandle::tryGetInteger(int64_t& value) const
{
    return isValid() && PropertyAccessor::tryGetInteger(_slot, _instances.front(), value);
}

bool PropertyHandle::setInteger(int64_t value) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setInteger(_slot, instance, value));
    }
    notifyIfChanged(changed);
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
        if (!PropertyAccessor::tryGetInteger(_slot, instance, value)) {
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
        changed = anyChanged(changed, PropertyAccessor::setInteger(_slot, _instances[i], values[i]));
    }
    notifyIfChanged(changed);
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
        if (!PropertyAccessor::tryGet(_slot, instance, value)) {
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
        changed = anyChanged(changed, PropertyAccessor::set(_slot, _instances[i], values[i] != 0));
    }
    notifyIfChanged(changed);
    return changed;
}

bool PropertyHandle::tryGetEnumIndex(int& index) const
{
    return isValid() && PropertyAccessor::tryGetEnumIndex(_slot, _instances.front(), index);
}

bool PropertyHandle::enumLabels(std::vector<std::string>& labels) const
{
    return isValid() && PropertyAccessor::enumLabels(_slot, labels);
}

bool PropertyHandle::setEnumByIndex(int index) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setEnumByIndex(_slot, instance, index));
    }
    notifyIfChanged(changed);
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
        if (!PropertyAccessor::tryGetEnumValue(_slot, instance, value)) {
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
        changed = anyChanged(changed, PropertyAccessor::setEnumValue(_slot, _instances[i], values[i]));
    }
    notifyIfChanged(changed);
    return changed;
}

bool PropertyHandle::tryGetColor(glm::vec4& value) const
{
    return isValid() && PropertyAccessor::tryGetColor(_slot, _instances.front(), value);
}

bool PropertyHandle::setColor(const glm::vec4& value) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setColor(_slot, instance, value));
    }
    notifyIfChanged(changed);
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
        if (!PropertyAccessor::tryGetColor(_slot, instance, value)) {
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
        changed = anyChanged(changed, PropertyAccessor::setColor(_slot, _instances[i], values[i]));
    }
    notifyIfChanged(changed);
    return changed;
}

bool PropertyHandle::tryGetAssetPath(std::string& value) const
{
    return isValid() && PropertyAccessor::tryGetAssetPath(_slot, _instances.front(), value);
}

bool PropertyHandle::setAssetPath(const std::string& value) const
{
    if (!isEditable()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::setAssetPath(_slot, instance, value));
    }
    notifyIfChanged(changed);
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
        if (!PropertyAccessor::tryGetAssetPath(_slot, instance, value)) {
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
        changed = anyChanged(changed, PropertyAccessor::setAssetPath(_slot, _instances[i], values[i]));
    }
    notifyIfChanged(changed);
    return changed;
}

bool PropertyHandle::hasAssetResolveError() const
{
    if (!isAssetRef()) {
        return false;
    }
    return std::any_of(_instances.begin(), _instances.end(), [&](void* instance) {
        return PropertyAccessor::hasAssetResolveError(_slot, instance);
    });
}

std::string PropertyHandle::validationError() const
{
    if (!isValid()) {
        return {};
    }
    return PropertyAccessor::validationError(_slot, _instances.front());
}

bool PropertyHandle::tryGetManipulateSpec(reflection::Meta::ManipulateSpec& spec) const
{
    return isValid() && PropertyAccessor::tryGetManipulateSpec(*_slot.property, spec);
}

bool PropertyHandle::canMutateContainer() const
{
    return isEditable() &&
           (PropertyAccessor::isDynamicSequence(*_slot.property) || PropertyAccessor::isMapOfLeaves(*_slot.property));
}

size_t PropertyHandle::containerSize() const
{
    if (!isValid()) {
        return 0;
    }
    return PropertyAccessor::containerSize(*_slot.property, _instances.front());
}

bool PropertyHandle::appendEmpty() const
{
    if (!canMutateContainer()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::appendEmpty(*_slot.property, instance));
    }
    notifyIfChanged(changed);
    return changed;
}

bool PropertyHandle::removeAt() const
{
    if (!isEditable() || _slot.elementIndex < 0) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::removeAt(*_slot.property, instance, _slot.elementIndex));
    }
    notifyIfChanged(changed);
    return changed;
}

bool PropertyHandle::removeAtIndex(int index) const
{
    if (!isEditable() || index < 0) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::removeAt(*_slot.property, instance, index));
    }
    notifyIfChanged(changed);
    return changed;
}

bool PropertyHandle::insertEmptyAt(int index) const
{
    if (!canMutateContainer()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::insertEmptyAt(*_slot.property, instance, index));
    }
    notifyIfChanged(changed);
    return changed;
}

bool PropertyHandle::clearContainer() const
{
    if (!canMutateContainer()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::clearContainer(*_slot.property, instance));
    }
    notifyIfChanged(changed);
    return changed;
}

bool PropertyHandle::removeMapKey() const
{
    if (!isEditable() || !_slot.mapKey.has_value()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::removeMapKey(*_slot.property, instance, *_slot.mapKey));
    }
    notifyIfChanged(changed);
    return changed;
}

bool PropertyHandle::insertMapKey(std::string_view key) const
{
    if (!canMutateContainer()) {
        return false;
    }
    bool changed = false;
    for (void* instance : _instances) {
        changed = anyChanged(changed, PropertyAccessor::insertMapKey(*_slot.property, instance, key));
    }
    notifyIfChanged(changed);
    return changed;
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
    PropertyHandle handle{ownerType, std::move(instances), it->second};
    handle.setVec3Setter(std::move(setter));
    return handle;
}

} // namespace ya
