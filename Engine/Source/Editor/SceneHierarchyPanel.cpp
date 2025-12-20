#include "SceneHierarchyPanel.h"

#include "ECS/Component.h"
#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Scene/Scene.h"
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>


namespace ya
{

void SceneHierarchyPanel::sceneTree()
{
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
            drawEntityNode(*ent);
        }
    }

    // Right-click on blank space to deselect
    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
    {
        _selection = Entity();
    }

    ImGui::End();
}


void SceneHierarchyPanel::detailsView()
{
    // Properties panel
    ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Properties"))
    {
        if (_selection)
        {
            drawComponents(_selection);
        }
    }
    ImGui::End();
}

void SceneHierarchyPanel::onImGuiRender()
{
    sceneTree();
    detailsView();
}

void SceneHierarchyPanel::drawEntityNode(Entity &entity)
{
    const char *name     = entity._name.c_str();
    bool        selected = (entity == _selection);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    if (selected)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool opened = ImGui::TreeNodeEx((void *)(intptr_t)entity.getId(), flags, "%s", name);

    if (ImGui::IsItemClicked())
    {
        setSelection(entity);
    }

    if (opened)
    {
        ImGui::TreePop();
    }
}

template <class T, class UIFunction>
static void drawComponent(const std::string &name, Entity entity, UIFunction ui_func)
{
    const auto treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen |
                               ImGuiTreeNodeFlags_AllowItemOverlap |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_FramePadding |
                               ImGuiTreeNodeFlags_Framed;

    if (entity.hasComponent<T>())
    {
        auto  *component                = entity.getComponent<T>();
        ImVec2 content_region_available = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4, 4});
        float line_height = ImGui::GetFont()->LegacySize + ImGui::GetStyle().FramePadding.y * 2.f;
        ImGui::Separator();

        bool bOpen = ImGui::TreeNodeEx((void *)typeid(T).hash_code(), treeNodeFlags, "%s", name.c_str());

        ImGui::PopStyleVar();
        ImGui::SameLine(content_region_available.x - line_height * 0.5f);

        if (ImGui::Button("+", ImVec2{line_height, line_height})) {
            ImGui::OpenPopup("ComponentSettings");
        }

        bool bRemoveComponent = false;

        if (ImGui::BeginPopup("ComponentSettings"))
        {
            if (ImGui::MenuItem("Remove Component")) {
                bRemoveComponent = true;
            }
            ImGui::EndPopup();
        }

        if (bOpen)
        {
            ui_func(component);

            ImGui::TreePop();
        }

        if (bRemoveComponent) {
            entity.removeComponent<T>();
        }
    }
}


void SceneHierarchyPanel::drawComponents(Entity &entity)
{
    if (!entity)
    {
        return;
    }

    // Draw entity name at top
    ImGui::Text("Entity ID: %u", entity.getId());
    ImGui::Separator();

    drawComponent<TransformComponent>("Transform", entity, [](TransformComponent *tc) {
        bool bDirty = false;
        bDirty |= ImGui::DragFloat3("Position", glm::value_ptr(tc->_position), 0.1f);
        bDirty |= ImGui::DragFloat3("Rotation", glm::value_ptr(tc->_rotation), 0.1f);
        bDirty |= ImGui::DragFloat3("Scale", glm::value_ptr(tc->_scale), 0.1f);
        tc->bDirty = bDirty;
    });

    drawComponent<LitMaterialComponent>("Lit Material", entity, [](LitMaterialComponent *lmc) {
        for (auto *mesh : lmc->_meshes) {
            ImGui::Text("Mesh: %s", mesh->_name.c_str());
        }
        for (auto [material, meshId] : lmc->getMaterial2MeshIDs()) {
            auto LitMat = material->as<LitMaterial>();
            bool bDirty = false;
            bDirty |= ImGui::ColorEdit3("Object Color", glm::value_ptr(LitMat->uParams.objectColor));
            if (bDirty) {
                LitMat->setParamDirty(true);
            }
        }
    });
    drawComponent<PointLightComponent>("Point Light", entity, [](PointLightComponent *plc) {
        bool bDirty = false;
        bDirty |= ImGui::ColorEdit3("Color", glm::value_ptr(plc->_color));
        bDirty |= ImGui::DragFloat("Intensity", &plc->_intensity, 0.1f, 0.0f, 100.0f);
        bDirty |= ImGui::DragFloat("Radius", &plc->_range, 0.1f, 0.0f, 100.0f);
    });
}

void SceneHierarchyPanel::setSelection(Entity newSelection)
{
    if (_selection != newSelection)
    {
        _lastSelection = _selection;
        _selection     = newSelection;
    }
}

} // namespace ya
