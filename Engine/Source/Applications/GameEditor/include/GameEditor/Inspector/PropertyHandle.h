#pragma once

#include "Core/TypeIndex.h"

#include "Core/Reflection/MetadataSupport.h"

#include "GameEditor/UI/EditorAssetPicker.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <functional>

struct Property;

namespace ya
{

/// Reflection-backed access shared by handwritten and automatic property editors.
/// This type is editor/data oriented and knows nothing about ImGui or WidgetTree.
class PropertyHandle final
{
  public:
    PropertyHandle() = default;
    using Vec3Setter = std::function<void(void*, const glm::vec3&)>;
    using ChangeHook = std::function<void()>;

    PropertyHandle(type_index_t ownerType,
                   std::vector<void*> instances,
                   const Property* property,
                   Vec3Setter setter = {},
                   int elementIndex = -1);

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

    void setVec3Setter(Vec3Setter setter) { _vec3Setter = std::move(setter); }
    void setChangeHook(ChangeHook hook) { _changeHook = std::move(hook); }

    [[nodiscard]] bool tryGetVec2(glm::vec2& value) const;
    bool setVec2(const glm::vec2& value) const;
    [[nodiscard]] std::vector<glm::vec2> copyVec2() const;
    bool restoreVec2(const std::vector<glm::vec2>& values) const;
    [[nodiscard]] bool tryGetVec3(glm::vec3& value) const;
    bool setVec3(const glm::vec3& value) const;
    [[nodiscard]] std::vector<glm::vec3> copyVec3() const;
    bool restoreVec3(const std::vector<glm::vec3>& values) const;
    [[nodiscard]] bool tryGetVec4(glm::vec4& value) const;
    bool setVec4(const glm::vec4& value) const;
    [[nodiscard]] std::vector<glm::vec4> copyVec4() const;
    bool restoreVec4(const std::vector<glm::vec4>& values) const;
    [[nodiscard]] bool tryGetFloat(float& value) const;
    bool setFloat(float value) const;
    [[nodiscard]] std::vector<float> copyFloat() const;
    bool restoreFloat(const std::vector<float>& values) const;
    [[nodiscard]] bool tryGetInteger(int64_t& value) const;
    bool setInteger(int64_t value) const;
    [[nodiscard]] std::vector<int64_t> copyInteger() const;
    bool restoreInteger(const std::vector<int64_t>& values) const;
    [[nodiscard]] bool tryGetBool(bool& value) const;
    bool setBool(bool value) const;
    [[nodiscard]] std::vector<uint8_t> copyBool() const;
    bool restoreBool(const std::vector<uint8_t>& values) const;
    [[nodiscard]] bool tryGetString(std::string& value) const;
    bool setString(const std::string& value) const;
    [[nodiscard]] std::vector<std::string> copyString() const;
    bool restoreString(const std::vector<std::string>& values) const;

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

    [[nodiscard]] std::string validationError() const;
    [[nodiscard]] bool tryGetManipulateSpec(reflection::Meta::ManipulateSpec& spec) const;

  private:
    type_index_t _ownerType = 0;
    std::vector<void*> _instances;
    const Property* _property = nullptr;
    int _elementIndex = -1;
    Vec3Setter _vec3Setter;
    ChangeHook _changeHook;
};

class PropertyHandleFactory final
{
  public:
    static PropertyHandle make(type_index_t ownerType, std::vector<void*> instances, std::string_view propertyName, PropertyHandle::Vec3Setter setter = {});
};

} // namespace ya
