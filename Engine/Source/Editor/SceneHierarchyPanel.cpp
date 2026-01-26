#include "SceneHierarchyPanel.h"
#include "Core/Debug/Instrumentor.h"
#include "EditorLayer.h"

#include "Core/System/VirtualFileSystem.h"
#include "ECS/Component.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Scene/Node.h"
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
        // Render Node hierarchy tree
        Node *rootNode = _context->getRootNode();
        if (rootNode && rootNode->hasChildren())
        {
            // Recursively render all children of root node
            for (Node *child : rootNode->getChildren())
            {
                drawNodeRecursive(child);
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Standalone Entities:");
        // Render standalone entities (entities without Node hierarchy)
        // These are entities created with createEntity() instead of createNodeEntity()
        renderStandaloneEntities();

        // Right-click on blank space - create entity menu
        if (ImGui::BeginPopupContextWindow("SceneHierarchyContextMenu", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
        {
            if (ImGui::MenuItem("Create Empty Node"))
            {
                Node *newNode = _context->createNode3D("New Node");
                if (auto *node3D = dynamic_cast<Node3D *>(newNode)) {
                    setSelection(node3D->getEntity());
                }
            }

            if (ImGui::BeginMenu("Create 3D Object"))
            {
                if (ImGui::MenuItem("Cube"))
                {
                    Node *newNode = _context->createNode3D("Cube");
                    if (auto *node3D = dynamic_cast<Node3D *>(newNode)) {
                        Entity *newEntity = node3D->getEntity();
                        auto    mc        = newEntity->addComponent<MeshComponent>();
                        mc->setPrimitiveGeometry(EPrimitiveGeometry::Cube);
                        newEntity->addComponent<PhongMaterialComponent>();
                        setSelection(newEntity);
                    }
                }
                if (ImGui::MenuItem("Sphere"))
                {
                    Node *newNode = _context->createNode3D("Sphere");
                    if (auto *node3D = dynamic_cast<Node3D *>(newNode)) {
                        Entity *newEntity = node3D->getEntity();
                        auto    mc        = newEntity->addComponent<MeshComponent>();
                        mc->setPrimitiveGeometry(EPrimitiveGeometry::Sphere);
                        newEntity->addComponent<PhongMaterialComponent>();
                        setSelection(newEntity);
                    }
                }
                if (ImGui::MenuItem("Plane"))
                {
                    Node *newNode = _context->createNode3D("Plane");
                    if (auto *node3D = dynamic_cast<Node3D *>(newNode)) {
                        Entity *newEntity = node3D->getEntity();
                        auto    mc        = newEntity->addComponent<MeshComponent>();
                        mc->setPrimitiveGeometry(EPrimitiveGeometry::Quad);
                        newEntity->addComponent<PhongMaterialComponent>();
                        setSelection(newEntity);
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Create Point Light"))
            {
                Node *newNode = _context->createNode3D("Point Light");
                if (auto *node3D = dynamic_cast<Node3D *>(newNode)) {
                    Entity *newEntity = node3D->getEntity();
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

void SceneHierarchyPanel::drawNodeRecursive(Node *node)
{
    if (!node) {
        return;
    }

    // Cast to Node3D to access Entity
    auto   *node3D = dynamic_cast<Node3D *>(node);
    Entity *entity = node3D ? node3D->getEntity() : nullptr;
    if (!entity) {
        return;
    }

    auto &name     = getNodeName(node);
    bool  selected = (entity == _selection);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    // If node has children, allow expanding
    if (!node->hasChildren()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool opened = ImGui::TreeNodeEx((void *)(intptr_t)entity->getId(), flags, "%s", name.c_str());

    if (ImGui::IsItemClicked()) {
        setSelection(entity);
    }

    // Right-click context menu for node
    bool bEntityDeleted = false;
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Duplicate")) {
            // TODO: Implement entity duplication with hierarchy
            YA_CORE_INFO("Duplicate entity: {}", name.c_str());
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete")) {
            bEntityDeleted = true;
        }

        ImGui::EndPopup();
    }

    if (opened && node->hasChildren()) {
        // Recursively render children
        for (Node *child : node->getChildren()) {
            drawNodeRecursive(child);
        }
        ImGui::TreePop();
    }

    // Delete entity after UI rendering
    if (bEntityDeleted) {
        if (_selection == entity) {
            setSelection(nullptr);
        }
        _context->destroyEntity(entity);
    }
}

void SceneHierarchyPanel::renderStandaloneEntities()
{
    // Find entities that are not managed by any Node
    // These are entities created with createEntity() instead of createNodeEntity()
    auto view = _context->getRegistry().view<TransformComponent>();
    for (auto entityHandle : view) {
        Entity *entity = _context->getEntityByEnttID(entityHandle);
        if (entity && !_context->getNodeByEntity(entityHandle)) {
            // This entity is not managed by Node hierarchy, render as standalone
            drawFlatEntity(*entity);
        }
    }
}

void SceneHierarchyPanel::drawFlatEntity(Entity &entity)
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

const std::string &SceneHierarchyPanel::getNodeName(Node *node)
{
    // ★ 优先使用 Node 的名字（树状结构中的名字）
    return node->getName();
}



} // namespace ya