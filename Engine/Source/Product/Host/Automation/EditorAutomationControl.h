#pragma once

#include "Foundation/Core/Module/Module.h"

#include <glm/vec3.hpp>

namespace ya
{

struct Scene;

inline constexpr FInterfaceId YA_EDITOR_AUTOMATION_CONTROL_INTERFACE =
    makeInterfaceId("ya.EditorAutomationControl");

struct IEditorAutomationControl
{
    virtual ~IEditorAutomationControl() = default;

    [[nodiscard]] virtual Scene* getAuthoringScene() const = 0;
    virtual bool setEditorCameraTransform(const glm::vec3& position, const glm::vec3& rotation) = 0;
    virtual bool focusEditorCameraOnWorldPoint(const glm::vec3& target,
                                               float             distance,
                                               float             heightOffset) = 0;
};

} // namespace ya


