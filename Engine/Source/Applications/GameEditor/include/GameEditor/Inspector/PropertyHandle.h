#pragma once

#include "Core/TypeIndex.h"

#include <glm/vec3.hpp>
#include <string>
#include <string_view>
#include <vector>
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

    PropertyHandle(type_index_t ownerType, std::vector<void*> instances, const Property* property, Vec3Setter setter = {});

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool isEditable() const;
    [[nodiscard]] bool isMixed() const;
    [[nodiscard]] const std::string& getName() const;

    [[nodiscard]] bool tryGetVec3(glm::vec3& value) const;
    bool setVec3(const glm::vec3& value) const;
    [[nodiscard]] bool tryGetFloat(float& value) const;
    bool setFloat(float value) const;
    [[nodiscard]] bool tryGetBool(bool& value) const;
    bool setBool(bool value) const;
    [[nodiscard]] bool tryGetString(std::string& value) const;
    bool setString(const std::string& value) const;

  private:
    type_index_t _ownerType = 0;
    std::vector<void*> _instances;
    const Property* _property = nullptr;
    Vec3Setter _vec3Setter;
};

class PropertyHandleFactory final
{
  public:
    static PropertyHandle make(type_index_t ownerType, std::vector<void*> instances, std::string_view propertyName, PropertyHandle::Vec3Setter setter = {});
};

} // namespace ya
