#pragma once

#include "Core/TypeIndex.h"

#include "Core/Reflection/MetadataSupport.h"
#include "Core/Reflection/PropertyAccessor.h"

#include "GameEditor/UI/EditorAssetPicker.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <cstdint>
#include <functional>
#include <utility>

struct Property;

namespace ya
{

/// Editor adapter over a reflected slot: multi-select, undo snapshots, and UI hooks.
/// Typed payload access is `tryGet<T>` / `set<T>` / `copy<T>` / `restore`.
class PropertyHandle final
{
  public:
    PropertyHandle() = default;
    using Vec3Setter = std::function<void(void*, const glm::vec3&)>;
    using ChangeHook = std::function<void()>;
    using InstanceResolver = std::function<void*()>;
    struct FInstanceBinding
    {
        std::string identity;
        InstanceResolver resolver;
    };

    PropertyHandle(type_index_t ownerType,
                   std::vector<void*> instances,
                   reflection::FPropertySlot slot);

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool isEditable() const;
    [[nodiscard]] bool isEnum() const;
    [[nodiscard]] bool isColor() const;
    [[nodiscard]] bool isAssetRef() const;
    [[nodiscard]] std::optional<EEditorAssetPickerKind> assetRefKind() const;
    [[nodiscard]] bool isMixed() const;
    [[nodiscard]] bool isMixedVec3Axis(int axis) const;
    [[nodiscard]] bool isMixedVecAxis(int axis, int componentCount) const;
    [[nodiscard]] const std::string& getName() const;

    [[nodiscard]] const reflection::FPropertySlot& slot() const { return _slot; }
    void setVec3Setter(Vec3Setter setter) { _vec3Setter = std::move(setter); }
    void setChangeHook(ChangeHook hook) { _changeHook = std::move(hook); }
    void setInstanceResolvers(std::vector<InstanceResolver> resolvers)
    {
        _instanceBindings.clear();
        _instanceResolvers = std::move(resolvers);
        refreshInstances();
    }
    void setInstanceBindings(std::vector<FInstanceBinding> bindings)
    {
        _instanceBindings = std::move(bindings);
        _instanceResolvers.clear();
        _instanceResolvers.reserve(_instanceBindings.size());
        for (const FInstanceBinding& binding : _instanceBindings) {
            _instanceResolvers.push_back(binding.resolver);
        }
        refreshInstances();
    }
    void setOwnerPath(std::vector<reflection::FPropertySlot> path)
    {
        _ownerPath = std::move(path);
        refreshInstances();
    }
    [[nodiscard]] const std::vector<FInstanceBinding>& instanceBindings() const { return _instanceBindings; }
    [[nodiscard]] std::vector<void*> resolvedInstances() const
    {
        refreshInstances();
        return _instances;
    }

    template <typename T>
    [[nodiscard]] bool tryGet(T& value) const
    {
        return isValid() && reflection::PropertyAccessor::tryGet(_slot, _instances.front(), value);
    }

    template <typename T>
    bool set(const T& value) const
    {
        if (!isEditable() || reflection::PropertyAccessor::valueType(_slot) != ya::type_index_v<T> ||
            !canAccessAllMutable()) {
            return false;
        }
        std::vector<T> before;
        before.reserve(_instances.size());
        for (void* instance : _instances) {
            T current{};
            if (!reflection::PropertyAccessor::tryGet(_slot, instance, current)) {
                return false;
            }
            before.push_back(current);
        }
        bool changed = false;
        size_t applied = 0;
        bool failed = false;
        for (void* instance : _instances) {
            if constexpr (std::is_same_v<T, glm::vec3>) {
                glm::vec3 current{};
                if (!reflection::PropertyAccessor::tryGet(_slot, instance, current) || current == value) {
                    ++applied;
                    continue;
                }
                if (_vec3Setter) {
                    _vec3Setter(instance, value);
                    changed = true;
                    ++applied;
                }
                else {
                    const reflection::FPropertyMutationResult result =
                        reflection::PropertyAccessor::setResult(_slot, instance, value);
                    if (result.changed()) {
                        changed = true;
                        ++applied;
                    }
                    else if (!result.accepted()) {
                        failed = true;
                        break;
                    }
                }
            }
            else {
                const reflection::FPropertyMutationResult result =
                    reflection::PropertyAccessor::setResult(_slot, instance, value);
                if (result.changed()) {
                    changed = true;
                    ++applied;
                }
                else if (!result.accepted()) {
                    failed = true;
                    break;
                }
            }
        }
        if (failed) {
            for (size_t index = 0; index < applied; ++index) {
                if constexpr (std::is_same_v<T, glm::vec3>) {
                    if (_vec3Setter) {
                        _vec3Setter(_instances[index], before[index]);
                    }
                    else {
                        (void)reflection::PropertyAccessor::set(_slot, _instances[index], before[index]);
                    }
                }
                else {
                    (void)reflection::PropertyAccessor::set(_slot, _instances[index], before[index]);
                }
            }
            return false;
        }
        notifyIfChanged(changed);
        return changed;
    }

    template <typename T>
    [[nodiscard]] std::vector<T> copy() const
    {
        static_assert(!std::is_same_v<T, bool>, "bool snapshots use copyBool()");
        std::vector<T> values;
        if (!isValid()) {
            return values;
        }
        values.reserve(_instances.size());
        for (void* instance : _instances) {
            T value{};
            if (!reflection::PropertyAccessor::tryGet(_slot, instance, value)) {
                return {};
            }
            values.push_back(std::move(value));
        }
        return values;
    }

    template <typename T>
    bool restore(const std::vector<T>& values) const
    {
        static_assert(!std::is_same_v<T, bool>, "bool snapshots use restoreBool()");
        if (!isEditable() || values.size() != _instances.size() ||
            reflection::PropertyAccessor::valueType(_slot) != ya::type_index_v<T> ||
            !canAccessAllMutable()) {
            return false;
        }
        std::vector<T> before;
        before.reserve(_instances.size());
        for (void* instance : _instances) {
            T current{};
            if (!reflection::PropertyAccessor::tryGet(_slot, instance, current)) {
                return false;
            }
            before.push_back(current);
        }
        bool changed = false;
        size_t applied = 0;
        bool failed = false;
        for (size_t i = 0; i < _instances.size(); ++i) {
            if constexpr (std::is_same_v<T, glm::vec3>) {
                glm::vec3 current{};
                if (!reflection::PropertyAccessor::tryGet(_slot, _instances[i], current) || current == values[i]) {
                    ++applied;
                    continue;
                }
                if (_vec3Setter) {
                    _vec3Setter(_instances[i], values[i]);
                    changed = true;
                    ++applied;
                }
                else {
                    const reflection::FPropertyMutationResult result =
                        reflection::PropertyAccessor::setResult(_slot, _instances[i], values[i]);
                    if (result.changed()) {
                        changed = true;
                        ++applied;
                    }
                    else if (!result.accepted()) {
                        failed = true;
                        break;
                    }
                }
            }
            else {
                const reflection::FPropertyMutationResult result =
                    reflection::PropertyAccessor::setResult(_slot, _instances[i], values[i]);
                if (result.changed()) {
                    changed = true;
                    ++applied;
                }
                else if (!result.accepted()) {
                    failed = true;
                    break;
                }
            }
        }
        if (failed) {
            for (size_t index = 0; index < applied; ++index) {
                if constexpr (std::is_same_v<T, glm::vec3>) {
                    if (_vec3Setter) {
                        _vec3Setter(_instances[index], before[index]);
                    }
                    else {
                        (void)reflection::PropertyAccessor::set(_slot, _instances[index], before[index]);
                    }
                }
                else {
                    (void)reflection::PropertyAccessor::set(_slot, _instances[index], before[index]);
                }
            }
            return false;
        }
        notifyIfChanged(changed);
        return changed;
    }

    [[nodiscard]] bool tryGetInteger(int64_t& value) const;
    bool setInteger(int64_t value) const;
    [[nodiscard]] std::vector<int64_t> copyInteger() const;
    bool restoreInteger(const std::vector<int64_t>& values) const;

    [[nodiscard]] std::vector<uint8_t> copyBool() const;
    bool restoreBool(const std::vector<uint8_t>& values) const;

    [[nodiscard]] bool tryGetEnumIndex(int& index) const;
    [[nodiscard]] bool enumLabels(std::vector<std::string>& labels) const;
    bool setEnumByIndex(int index) const;
    [[nodiscard]] std::vector<int64_t> copyEnum() const;
    bool restoreEnum(const std::vector<int64_t>& values) const;

    [[nodiscard]] bool tryGetColor(glm::vec4& value) const;
    bool setColor(const glm::vec4& value) const;
    [[nodiscard]] std::vector<glm::vec4> copyColor() const;
    bool restoreColor(const std::vector<glm::vec4>& values) const;

    [[nodiscard]] bool tryGetAssetPath(std::string& value) const;
    bool setAssetPath(const std::string& value) const;
    [[nodiscard]] std::vector<std::string> copyAssetPath() const;
    bool restoreAssetPath(const std::vector<std::string>& values) const;
    [[nodiscard]] bool hasAssetResolveError() const;

    [[nodiscard]] bool canMutateContainer() const;
    [[nodiscard]] size_t containerSize() const;
    bool appendEmpty() const;
    bool removeAt() const;
    bool removeAtIndex(int index) const;
    bool insertEmptyAt(int index) const;
    bool clearContainer() const;
    bool removeMapKey() const;
    bool insertMapKey(std::string_view key) const;

    [[nodiscard]] std::string validationError() const;
    [[nodiscard]] bool tryGetManipulateSpec(reflection::Meta::ManipulateSpec& spec) const;

  private:
    void refreshInstances() const;
    [[nodiscard]] bool canAccessAllMutable() const;
    [[nodiscard]] bool canRemoveAtIndexFromAll(int index) const;
    [[nodiscard]] bool canInsertAtIndexIntoAll(int index) const;
    [[nodiscard]] bool canRemoveMapKeyFromAll() const;
    [[nodiscard]] bool canInsertMapKeyIntoAll(std::string_view key) const;

    void notifyIfChanged(bool changed) const
    {
        if (changed && _changeHook) {
            _changeHook();
        }
    }

    type_index_t _ownerType = 0;
    mutable std::vector<void*> _instances;
    std::vector<InstanceResolver> _instanceResolvers;
    std::vector<FInstanceBinding> _instanceBindings;
    std::vector<reflection::FPropertySlot> _ownerPath;
    reflection::FPropertySlot _slot;
    Vec3Setter _vec3Setter;
    ChangeHook _changeHook;
};

class PropertyHandleFactory final
{
  public:
    static PropertyHandle make(type_index_t ownerType, std::vector<void*> instances, std::string_view propertyName, PropertyHandle::Vec3Setter setter = {});
};

} // namespace ya
