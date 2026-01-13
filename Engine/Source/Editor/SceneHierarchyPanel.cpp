#include "SceneHierarchyPanel.h"
#include "Core/Debug/Instrumentor.h"
#include "EditorLayer.h"

#include "Core/System/FileSystem.h"
#include "ECS/Component.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Scene/Scene.h"
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace ya
{

void SceneHierarchyPanel::onImGuiRender()
{
    YA_PROFILE_FUNCTION();
    sceneTree();
}

void SceneHierarchyPanel::setSelection(Entity *entity)
{
    _selection = entity;
    _owner->setSelectedEntity(entity);
}

void SceneHierarchyPanel::sceneTree()
{
    YA_PROFILE_FUNCTION();
    ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene Hierarchy"))
    {
        ImGui::End();
        return;
    }

    if (_context)
    {
        auto view = _context->getRegistry().view<TransformComponent>();
        for (auto entity : view)
        {
            Entity *ent = _context->getEntityByEnttID(entity);
            if (ent)
            {
                drawEntityNode(*ent);
            }
        }
    }

    // Right-click on blank space to deselect
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
    {
        if (!ImGui::IsAnyItemHovered())
        {
            _selection = nullptr;
        }
    }

    ImGui::End();
}


void SceneHierarchyPanel::drawEntityNode(Entity &entity)
{
    if (!entity)
    {
        return;
    }

    const char *name     = entity.getName().c_str();
    bool        selected = (&entity == _selection);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool opened = ImGui::TreeNodeEx((void *)(intptr_t)entity.getId(), flags, "%s", name);

    if (ImGui::IsItemClicked())
    {
        setSelection(&entity);
    }

    if (opened)
    {
        ImGui::TreePop();
    }
}


} // namespace ya