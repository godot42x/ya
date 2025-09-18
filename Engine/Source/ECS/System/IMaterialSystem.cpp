#include "IMaterialSystem.h"

#include "Core/App/App.h"
#include "ECS/Component/CameraComponent.h"
#include "Render/Core/RenderTarget.h"

#include "imgui.h"

namespace ya
{

void IMaterialSystem::onRenderGUI()
{
    ImGui::CollapsingHeader(_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PushID(_label.c_str());
    ImGui::Checkbox("Reverse Viewport Y", &bReverseViewportY);
    ImGui::Checkbox("Enabled", &bEnabled);
    ImGui::PopID();
}

void IMaterialSystem::onEndRenderGUI()
{
}

App *IMaterialSystem::getApp() const
{
    return App::get();
}

Scene *IMaterialSystem::getScene() const
{
    if (getApp()) {
        return getApp()->getScene();
    }
    return nullptr;
}

VulkanRender *IMaterialSystem::getVulkanRender() const
{
    if (getApp()) {
        return getApp()->getRender<VulkanRender>();
    }
    return nullptr;
}



} // namespace ya
