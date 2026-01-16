#include "SceneHierarchyPanel.h"
#include "Core/Debug/Instrumentor.h"
#include "EditorLayer.h"

#include "Core/System/VirtualFileSystem.h"
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

        // Right-click on blank space - create entity menu
        if (ImGui::BeginPopupContextWindow("SceneHierarchyContextMenu", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
        {
            if (ImGui::MenuItem("Create Empty Entity"))
            {
                Entity *newEntity = _context->createEntity("New Entity");
                if (newEntity) {
                    newEntity->addComponent<TransformComponent>();
                    setSelection(newEntity);
                }
            }
            
            if (ImGui::BeginMenu("Create 3D Object"))
            {
                if (ImGui::MenuItem("Cube"))
                {
                    Entity *newEntity = _context->createEntity("Cube");
                    if (newEntity) {
                        auto tc = newEntity->addComponent<TransformComponent>();
                        auto lmc = newEntity->addComponent<LitMaterialComponent>();
                        lmc->_primitiveGeometry = EPrimitiveGeometry::Cube;
                        setSelection(newEntity);
                    }
                }
                if (ImGui::MenuItem("Sphere"))
                {
                    Entity *newEntity = _context->createEntity("Sphere");
                    if (newEntity) {
                        auto tc = newEntity->addComponent<TransformComponent>();
                        auto lmc = newEntity->addComponent<LitMaterialComponent>();
                        lmc->_primitiveGeometry = EPrimitiveGeometry::Sphere;
                        setSelection(newEntity);
                    }
                }
                if (ImGui::MenuItem("Plane"))
                {
                    Entity *newEntity = _context->createEntity("Plane");
                    if (newEntity) {
                        auto tc = newEntity->addComponent<TransformComponent>();
                        auto lmc = newEntity->addComponent<LitMaterialComponent>();
                        lmc->_primitiveGeometry = EPrimitiveGeometry::Quad;
                        setSelection(newEntity);
                    }
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::MenuItem("Create Point Light"))
            {
                Entity *newEntity = _context->createEntity("Point Light");
                if (newEntity) {
                    newEntity->addComponent<TransformComponent>();
                    newEntity->addComponent<PointLightComponent>();
                    setSelection(newEntity);
                }
            }
            
            ImGui::EndPopup();
        }
    }

    // Left-click on blank space to deselect
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

    // Right-click context menu for entity
    bool bEntityDeleted = false;
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Duplicate"))
        {
            // TODO: Implement entity duplication
            YA_CORE_INFO("Duplicate entity: {}", entity.getName());
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Delete"))
        {
            bEntityDeleted = true;
        }
        
        ImGui::EndPopup();
    }

    if (opened)
    {
        ImGui::TreePop();
    }

    // Delete entity after UI rendering to avoid invalidating iterators
    if (bEntityDeleted)
    {
        if (_selection == &entity) {
            setSelection(nullptr);
        }
        _context->destroyEntity(&entity);
    }
}


} // namespace ya