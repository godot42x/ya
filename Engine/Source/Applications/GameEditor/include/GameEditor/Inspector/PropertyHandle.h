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
        bool changed = false;
        for (void* instance : _instances) {
            if constexpr (std::is_same_v<T, glm::vec3>) {
                glm::vec3 current{};
                if (!reflection::PropertyAccessor::tryGet(_slot, instance, current) || current == value) {
                    continue;
                }
                if (_vec3Setter) {
                    _vec3Setter(instance, value);
                    changed = true;
                }
                else if (reflection::PropertyAccessor::set(_slot, instance, value)) {
                    changed = true;
                }
            }
            else if (reflection::PropertyAccessor::set(_slot, instance, value)) {
                changed = true;
            }
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
        bool changed = false;
        for (size_t i = 0; i < _instances.size(); ++i) {
            if constexpr (std::is_same_v<T, glm::vec3>) {
                glm::vec3 current{};
                if (!reflection::PropertyAccessor::tryGet(_slot, _instances[i], current) || current == values[i]) {
                    continue;
                }
                if (_vec3Setter) {
                    _vec3Setter(_instances[i], values[i]);
                    changed = true;
                }
                else if (reflection::PropertyAccessor::set(_slot, _instances[i], values[i])) {
                    changed = true;
                }
            }
            else if (reflection::PropertyAccessor::set(_slot, _instances[i], values[i])) {
                changed = true;
            }
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
    [[nodiscard]] bool canAccessAllMutable() const;

    void notifyIfChanged(bool changed) const
    {
        if (changed && _changeHook) {
            _changeHook();
        }
    }

    type_index_t _ownerType = 0;
    std::vector<void*> _instances;
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
