#include "SceneHierarchyPanel.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/Manager/Facade.h"
#include "EditorCommon.h"
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

void SceneHierarchyPanel::setSelection(Entity* entity)
{
    _selection = entity;
    _pendingScrollSelection = entity;
    _owner->setSelectedEntity(entity);
}

bool SceneHierarchyPanel::shouldAutoOpenForSelection(Node* node) const
{
    if (!node || !_context || !_pendingScrollSelection) {
        return false;
    }

    Node* selectedNode = _context->getNodeByEntity(_pendingScrollSelection);
    if (!selectedNode) {
        return false;
    }

    for (Node* current = selectedNode->getParent(); current != nullptr; current = current->getParent()) {
        if (current == node) {
            return true;
        }
    }

    return false;
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
        if (_selection && (!_selection->isValid() || _selection->getScene() != _context)) {
            _selection              = nullptr;
            _pendingScrollSelection = nullptr;
        }

        // Render Node hierarchy tree
        Node* rootNode = _context->getRootNode();
        if (rootNode && rootNode->hasChildren())
        {
            // Recursively render all children of root node
            for (Node* child : rootNode->getChildren())
            {
                drawNodeRecursive(child);
            }
        }

        ImGui::InvisibleButton("##SceneHierarchyRootDropTarget", ImVec2(std::max(ImGui::GetContentRegionAvail().x, 1.0f), 8.0f));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(NODE_DRAG_DROP_PAYLOAD)) {
                if (payload->IsDelivery()) {
                    Node* draggedNode = *static_cast<Node* const*>(payload->Data);
                    queueMoveNode(draggedNode, nullptr, ENodeDropPosition::Into);
                }
            }
            ImGui::EndDragDropTarget();
        }

        flushPendingNodeMove();

        ImGui::Separator();
        ImGui::TextDisabled("Standalone Entities:");
        // Render standalone entities (entities without Node hierarchy)
        // These are entities created with createEntity() instead of createNodeEntity()
        renderStandaloneEntities();

        // Right-click on blank space - create entity menu
        {
            Node*       parentNode = getSelectedNode();
            ContextMenu ctx("SceneHierarchyContextMenu", ContextMenu::Type::BlankSpace);
            if (ctx.begin())
            {
                if (ctx.menuItem("Create Empty Node"))
                {
                    Node* newNode = _context->createNode3D("New Node", parentNode);
                    if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                        setSelection(node3D->getEntity());
                    }
                }

                if (ctx.beginMenu("Create 3D Object"))
                {
                    if (ctx.menuItem("Cube"))
                    {
                        Node* newNode = _context->createNode3D("Cube", parentNode);
                        if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                            Entity* newEntity = node3D->getEntity();
                            auto    mc        = newEntity->addComponent<MeshComponent>();
                            mc->setPrimitiveGeometry(EPrimitiveGeometry::Cube);
                            newEntity->addComponent<PhongMaterialComponent>();
                            setSelection(newEntity);
                        }
                    }
                    if (ctx.menuItem("Sphere"))
                    {
                        Node* newNode = _context->createNode3D("Sphere", parentNode);
                        if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                            Entity* newEntity = node3D->getEntity();
                            auto    mc        = newEntity->addComponent<MeshComponent>();
                            mc->setPrimitiveGeometry(EPrimitiveGeometry::Sphere);
                            newEntity->addComponent<PhongMaterialComponent>();
                            setSelection(newEntity);
                        }
                    }
                    if (ctx.menuItem("Plane"))
                    {
                        Node* newNode = _context->createNode3D("Plane", parentNode);
                        if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                            Entity* newEntity = node3D->getEntity();
                            auto    mc        = newEntity->addComponent<MeshComponent>();
                            mc->setPrimitiveGeometry(EPrimitiveGeometry::Quad);
                            newEntity->addComponent<PhongMaterialComponent>();
                            setSelection(newEntity);
                        }
                    }
                    ctx.endMenu();
                }

                if (ctx.menuItem("Create Point Light"))
                {
                    Node* newNode = _context->createNode3D("Point Light", parentNode);
                    if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                        Entity* newEntity = node3D->getEntity();
                        newEntity->addComponent<PointLightComponent>();
                        setSelection(newEntity);
                    }
                }

                ctx.end();
            }
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

void SceneHierarchyPanel::drawNodeRecursive(Node* node)
{
    if (!node) {
        return;
    }

    // Cast to Node3D to access Entity
    Entity* entity = node->getEntity();
    if (!entity) {
        return;
    }

    auto& name        = getNodeName(node);
    bool  selected    = (entity == _selection);
    bool  hadChildren = node->hasChildren();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    // If node has children, allow expanding
    if (!hadChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (shouldAutoOpenForSelection(node)) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    bool   opened    = ImGui::TreeNodeEx((void*)(intptr_t)entity->getId(), flags, "%s", name.c_str());
    ImVec2 itemMin   = ImGui::GetItemRectMin();
    ImVec2 itemMax   = ImGui::GetItemRectMax();
    bool   isHovered = ImGui::IsItemHovered();
    if (selected && _pendingScrollSelection == entity) {
        ImGui::SetScrollHereY(0.5f);
        _pendingScrollSelection = nullptr;
    }

    if (ImGui::IsItemClicked()) {
        setSelection(entity);
    }

    if (ImGui::BeginDragDropSource()) {
        Node* draggedNode = node;
        ImGui::SetDragDropPayload(NODE_DRAG_DROP_PAYLOAD, &draggedNode, sizeof(draggedNode));
        ImGui::TextUnformatted(name.c_str());
        ImGui::EndDragDropSource();
    }

    ENodeDropPosition hoveredDropPosition = ENodeDropPosition::Into;
    bool              isDropTargetHovered = false;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(NODE_DRAG_DROP_PAYLOAD)) {
            hoveredDropPosition = getDropPosition(itemMin.y, itemMax.y);
            isDropTargetHovered = isHovered;
            if (payload->IsDelivery()) {
                Node* draggedNode = *static_cast<Node* const*>(payload->Data);
                queueMoveNode(draggedNode, node, hoveredDropPosition);
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (isDropTargetHovered) {
        ImDrawList* drawList     = ImGui::GetWindowDrawList();
        ImU32       lineColor    = IM_COL32(80, 160, 255, 235);
        ImU32       bandColor    = IM_COL32(80, 160, 255, 70);
        ImU32       childFill    = IM_COL32(80, 160, 255, 30);
        float       lineStartX   = ImGui::GetCursorScreenPos().x - ImGui::GetTreeNodeToLabelSpacing();
        float       lineEndX     = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        float       bandHeight   = 8.0f;
        float       markerRadius = 3.5f;

        switch (hoveredDropPosition) {
            case ENodeDropPosition::Before: {
                ImVec2 bandMin(lineStartX, itemMin.y - bandHeight * 0.5f);
                ImVec2 bandMax(lineEndX, itemMin.y + bandHeight * 0.5f);
                ImVec2 lineStart(lineStartX, itemMin.y);
                ImVec2 lineEnd(lineEndX, itemMin.y);
                drawList->AddRectFilled(bandMin, bandMax, bandColor, 2.0f);
                drawList->AddLine(lineStart, lineEnd, lineColor, 2.0f);
                drawList->AddCircleFilled(lineStart, markerRadius, lineColor);
                ImGui::SetTooltip("插入到当前节点前");
                break;
            }
            case ENodeDropPosition::Into: {
                ImVec2 rectMin(itemMin.x + 14.0f, itemMin.y + 4.0f);
                ImVec2 rectMax(itemMax.x - 4.0f, itemMax.y - 4.0f);
                drawList->AddRectFilled(rectMin, rectMax, childFill, 3.0f);
                drawList->AddRect(rectMin, rectMax, lineColor, 3.0f, 0, 1.5f);
                ImGui::SetTooltip("作为当前节点的子节点");
                break;
            }
            case ENodeDropPosition::After: {
                ImVec2 bandMin(lineStartX, itemMax.y - bandHeight * 0.5f);
                ImVec2 bandMax(lineEndX, itemMax.y + bandHeight * 0.5f);
                ImVec2 lineStart(lineStartX, itemMax.y);
                ImVec2 lineEnd(lineEndX, itemMax.y);
                drawList->AddRectFilled(bandMin, bandMax, bandColor, 2.0f);
                drawList->AddLine(lineStart, lineEnd, lineColor, 2.0f);
                drawList->AddCircleFilled(lineStart, markerRadius, lineColor);
                ImGui::SetTooltip("插入到当前节点后");
                break;
            }
        }
    }

    // Right-click context menu for node
    bool bEntityDeleted = false;
    {
        ContextMenu ctx("NodeContextMenu##" + std::to_string(entity->getId()), ContextMenu::Type::EntityItem);
        if (ctx.begin()) {
            if (ctx.menuItem("Duplicate")) {
                // TODO: Implement entity duplication with hierarchy
                if (auto newNode = _context->duplicateNode(node)) {
                    YA_CORE_INFO("Duplicated entity: {}", newNode->getName());
                    Facade.timerManager.delayCall(
                        1,
                        [this, newNode]() {
                            setSelection(newNode->getEntity());
                        });
                }
            }

            ctx.separator();

            if (ctx.menuItem("Delete")) {
                bEntityDeleted = true;
            }

            ctx.end();
        }
    }

    if (opened && hadChildren) {
        // Recursively render children
        for (Node* child : node->getChildren()) {
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
        Entity* entity = _context->getEntityByEnttID(entityHandle);
        if (entity && !_context->getNodeByEntity(entityHandle)) {
            // This entity is not managed by Node hierarchy, render as standalone
            drawFlatEntity(*entity);
        }
    }
}

void SceneHierarchyPanel::drawFlatEntity(Entity& entity)
{
    if (!entity)
    {
        return;
    }

    const char* name     = entity.getName().c_str();
    bool        selected = (&entity == _selection);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool opened = ImGui::TreeNodeEx((void*)(intptr_t)entity.getId(), flags, "%s", name);
    if (selected && _pendingScrollSelection == &entity)
    {
        ImGui::SetScrollHereY(0.5f);
        _pendingScrollSelection = nullptr;
    }

    if (ImGui::IsItemClicked())
    {
        setSelection(&entity);
    }

    // Right-click context menu for entity
    bool bEntityDeleted = false;
    {
        ContextMenu ctx("FlatEntityContextMenu##" + std::to_string(entity.getId()), ContextMenu::Type::EntityItem);
        if (ctx.begin())
        {
            if (ctx.menuItem("Duplicate"))
            {
                // TODO: Implement entity duplication
                if (auto newNode = _context->duplicateNode(_context->getNodeByEntity(&entity))) {
                    YA_CORE_INFO("Duplicated entity: {}", newNode->getName());
                }
            }

            ctx.separator();

            if (ctx.menuItem("Delete"))
            {
                bEntityDeleted = true;
            }

            ctx.end();
        }
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

Node* SceneHierarchyPanel::getSelectedNode() const
{
    if (!_context || !_selection) {
        return nullptr;
    }

    return _context->getNodeByEntity(_selection);
}

SceneHierarchyPanel::ENodeDropPosition SceneHierarchyPanel::getDropPosition(float itemMinY, float itemMaxY) const
{
    float itemHeight      = itemMaxY - itemMinY;
    float boundaryPadding = std::clamp(itemHeight * 0.33f, 8.0f, 14.0f);
    float mouseY          = ImGui::GetMousePos().y;

    if (mouseY <= itemMinY + boundaryPadding) {
        return ENodeDropPosition::Before;
    }
    if (mouseY >= itemMaxY - boundaryPadding) {
        return ENodeDropPosition::After;
    }
    return ENodeDropPosition::Into;
}

void SceneHierarchyPanel::queueMoveNode(Node* draggedNode, Node* targetNode, ENodeDropPosition dropPosition)
{
    _pendingDraggedNode  = draggedNode;
    _pendingDropTarget   = targetNode;
    _pendingDropPosition = dropPosition;
}

void SceneHierarchyPanel::flushPendingNodeMove()
{
    if (!_pendingDraggedNode) {
        return;
    }

    Node*             draggedNode  = _pendingDraggedNode;
    Node*             targetNode   = _pendingDropTarget;
    ENodeDropPosition dropPosition = _pendingDropPosition;

    _pendingDraggedNode = nullptr;
    _pendingDropTarget  = nullptr;

    moveNode(draggedNode, targetNode, dropPosition);
}

bool SceneHierarchyPanel::moveNode(Node* draggedNode, Node* targetNode, ENodeDropPosition dropPosition)
{
    if (!_context || !draggedNode) {
        return false;
    }

    Node* rootNode = _context->getRootNode();
    if (!rootNode || draggedNode == rootNode) {
        return false;
    }

    Node*  newParent  = rootNode;
    size_t childIndex = rootNode->getChildCount();

    if (targetNode) {
        if (draggedNode == targetNode || draggedNode->isAncestorOf(targetNode)) {
            return false;
        }

        switch (dropPosition) {
            case ENodeDropPosition::Into:
                newParent  = targetNode;
                childIndex = targetNode->getChildCount();
                break;
            case ENodeDropPosition::Before:
            case ENodeDropPosition::After: {
                newParent = targetNode->getParent();
                if (!newParent) {
                    newParent = rootNode;
                }

                childIndex = newParent->getChildIndex(targetNode);
                if (childIndex == Node::NPOS) {
                    childIndex = newParent->getChildCount();
                }
                else if (dropPosition == ENodeDropPosition::After) {
                    ++childIndex;
                }
                break;
            }
        }
    }

    bool moved = _context->moveNode(draggedNode, newParent, childIndex);
    if (moved && draggedNode->getEntity()) {
        setSelection(draggedNode->getEntity());
    }
    return moved;
}

const std::string& SceneHierarchyPanel::getNodeName(Node* node)
{
    // ★ 优先使用 Node 的名字（树状结构中的名字）
    return node->getName();
}



} // namespace ya
