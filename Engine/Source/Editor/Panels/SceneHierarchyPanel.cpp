#include "Editor/Panels/SceneHierarchyPanel.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/Manager/Facade.h"
#include "Editor/EditorCommon.h"
#include "Editor/EditorLayer.h"


#include "Core/System/VirtualFileSystem.h"
#include "ECS/Component.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/Terrain/TerrainComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Scene/Node.h"
#include "Scene/Scene.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>


namespace ya
{

void SceneHierarchyPanel::onImGuiRender()
{
    YA_PROFILE_FUNCTION();
    sceneTree();
}

void SceneHierarchyPanel::setContext(Scene* scene)
{
    _context = scene;

    bool bSelectionChanged = false;
    if (!_selections.empty()) {
        const size_t before = _selections.size();
        std::erase_if(_selections, [&](Entity* e) { return !e || !e->isValid() || e->getScene() != scene; });
        bSelectionChanged = _selections.size() != before;
    }
    if (_primarySelection && (!_primarySelection->isValid() || _primarySelection->getScene() != scene)) {
        _primarySelection = _selections.empty() ? nullptr : _selections.front();
        bSelectionChanged = true;
    }
    if (_rangeAnchor && (!_rangeAnchor->isValid() || _rangeAnchor->getScene() != scene)) {
        _rangeAnchor = nullptr;
    }

    _selectedFolder        = nullptr;
    _renamingFolder        = nullptr;
    _pendingFolderDelete   = nullptr;
    _pendingScrollSelection = nullptr;
    _pendingDraggedNode    = nullptr;
    _pendingDropTarget     = nullptr;
    _pendingNodeDuplicate.clear();
    _pendingEntityDelete.clear();
    _searchBuffer[0] = '\0';

    if (bSelectionChanged || !_primarySelection) {
        notifyOwnerSelection();
    }
}

void SceneHierarchyPanel::setSelection(Entity* entity)
{
    _selectedFolder = nullptr;
    if (entity && entity->isValid()) {
        _selections       = {entity};
        _primarySelection = entity;
        _rangeAnchor      = entity;
    }
    else {
        _selections.clear();
        _primarySelection = nullptr;
        _rangeAnchor      = nullptr;
    }
    _pendingScrollSelection = entity;
    notifyOwnerSelection();
}

void SceneHierarchyPanel::handleEntityClick(Entity* entity)
{
    if (!entity || !entity->isValid()) {
        return;
    }
    _selectedFolder = nullptr;

    const bool bMulti = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
    const bool bRange = ImGui::GetIO().KeyShift;

    // Shift-click: range select between the anchor and the clicked entity using
    // the canonical flat (DFS tree + standalone) order.
    if (bRange && _rangeAnchor) {
        auto anchorIt = std::find(_flatEntities.begin(), _flatEntities.end(), _rangeAnchor);
        auto clickIt  = std::find(_flatEntities.begin(), _flatEntities.end(), entity);
        if (anchorIt != _flatEntities.end() && clickIt != _flatEntities.end()) {
            auto [rangeBegin, rangeEnd] = std::minmax(anchorIt, clickIt);
            _selections.assign(rangeBegin, std::next(rangeEnd));
            std::erase(_selections, entity);
            _selections.insert(_selections.begin(), entity);
            _primarySelection      = entity;
            _pendingScrollSelection = entity;
            notifyOwnerSelection();
            return;
        }
    }

    // Ctrl/Cmd-click: toggle membership, clicked entity becomes primary.
    if (bMulti) {
        auto it = std::find(_selections.begin(), _selections.end(), entity);
        if (it != _selections.end()) {
            _selections.erase(it);
            if (_primarySelection == entity) {
                _primarySelection = _selections.empty() ? nullptr : _selections.front();
            }
        }
        else {
            _selections.insert(_selections.begin(), entity);
            _primarySelection = entity;
        }
        _rangeAnchor           = entity;
        _pendingScrollSelection = entity;
        notifyOwnerSelection();
        return;
    }

    _selections       = {entity};
    _primarySelection = entity;
    _rangeAnchor      = entity;
    _pendingScrollSelection = entity;
    notifyOwnerSelection();
}

void SceneHierarchyPanel::replaceSelection(const std::vector<Entity*>& entities, Entity* primary)
{
    _selections       = entities;
    _primarySelection = primary ? primary : (_selections.empty() ? nullptr : _selections.front());
    _rangeAnchor      = _primarySelection;
    _selectedFolder   = nullptr;
    _pendingScrollSelection = _primarySelection;
    notifyOwnerSelection();
}

void SceneHierarchyPanel::notifyOwnerSelection()
{
    if (_owner) {
        _owner->setSelections(_selections, _primarySelection);
    }
}

void SceneHierarchyPanel::validateSelections()
{
    bool bChanged = false;
    if (!_selections.empty()) {
        const size_t before = _selections.size();
        std::erase_if(_selections, [&](Entity* e) { return !e || !e->isValid() || e->getScene() != _context; });
        bChanged = _selections.size() != before;
    }
    if (_primarySelection && (!_primarySelection->isValid() || _primarySelection->getScene() != _context)) {
        _primarySelection = _selections.empty() ? nullptr : _selections.front();
        bChanged          = true;
    }
    else if (_primarySelection && _primarySelection->isValid()) {
        auto it = std::find(_selections.begin(), _selections.end(), _primarySelection);
        if (it == _selections.end()) {
            _selections.insert(_selections.begin(), _primarySelection);
            bChanged = true;
        }
    }
    if (_rangeAnchor && (!_rangeAnchor->isValid() || _rangeAnchor->getScene() != _context)) {
        _rangeAnchor = nullptr;
    }
    if (bChanged) {
        notifyOwnerSelection();
    }
}

bool SceneHierarchyPanel::isSelected(Entity* entity) const
{
    return std::find(_selections.begin(), _selections.end(), entity) != _selections.end();
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
    if (!ImGui::Begin("Scene Hierarchy")) {
        ImGui::End();
        return;
    }

    if (_context) {
        validateSelections();

        // Search filter (lower-cased copy for case-insensitive matching)
        {
            std::string search(_searchBuffer);
            std::transform(search.begin(), search.end(), search.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            _searchLower = std::move(search);
        }

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputTextWithHint("##SceneHierarchySearch", "Search...", _searchBuffer, sizeof(_searchBuffer));

        buildFlatEntityList();

        // Render Node hierarchy tree
        Node* rootNode = _context->getRootNode();
        if (rootNode && rootNode->hasChildren()) {
            for (Node* child : rootNode->getChildren()) {
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
        renderStandaloneEntities();

        // Right-click on blank space - create menu
        {
            Node* parentNode = getSelectedNode();
            ContextMenu ctx("SceneHierarchyContextMenu", ContextMenu::Type::BlankSpace);
            if (ctx.begin()) {
                if (ctx.menuItem("Create Folder")) {
                    if (Node* folder = _context->createFolder("New Folder", parentNode)) {
                        _selectedFolder = folder;
                    }
                }
                ctx.separator();
                drawCreateMenuItems(parentNode);
                ctx.end();
            }
        }
    }

    // Left-click on blank space to deselect
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        if (!ImGui::IsAnyItemHovered()) {
            replaceSelection({}, nullptr);
        }
    }

    // Deferred batch actions and modals run after the tree render so node
    // mutation never happens while a parent's child list is being iterated.
    flushPendingActions();
    drawFolderRenamePopup();
    drawFolderDeletePopup();

    ImGui::End();
}

void SceneHierarchyPanel::buildFlatEntityList()
{
    _flatEntities.clear();
    if (Node* rootNode = _context->getRootNode()) {
        for (Node* child : rootNode->getChildren()) {
            collectEntities(child);
        }
    }

    // Standalone entities (no Node) come after the tree order.
    auto view = _context->getRegistry().view<TransformComponent>();
    for (auto entityHandle : view) {
        Entity* entity = _context->getEntityByEnttID(entityHandle);
        if (entity && !_context->getNodeByEntity(entityHandle)) {
            _flatEntities.push_back(entity);
        }
    }
}

void SceneHierarchyPanel::collectEntities(Node* node)
{
    if (!node) {
        return;
    }
    if (Entity* entity = node->getEntity()) {
        _flatEntities.push_back(entity);
    }
    for (Node* child : node->getChildren()) {
        collectEntities(child);
    }
}

bool SceneHierarchyPanel::matchesFilter(const std::string& name) const
{
    if (!isSearchActive()) {
        return true;
    }
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower.find(_searchLower) != std::string::npos;
}

bool SceneHierarchyPanel::subtreeMatchesFilter(Node* node) const
{
    if (!node) {
        return false;
    }
    if (matchesFilter(getNodeName(node))) {
        return true;
    }
    for (Node* child : node->getChildren()) {
        if (subtreeMatchesFilter(child)) {
            return true;
        }
    }
    return false;
}

void SceneHierarchyPanel::drawNodeRecursive(Node* node)
{
    if (!node) {
        return;
    }

    if (node->isFolder()) {
        drawFolderNode(node);
        return;
    }

    Entity* entity = node->getEntity();
    if (!entity) {
        return;
    }

    const bool bSearching = isSearchActive();
    const bool bSelfMatch = bSearching && matchesFilter(getNodeName(node));
    if (bSearching && !bSelfMatch && !subtreeMatchesFilter(node)) {
        return;
    }

    auto& name        = getNodeName(node);
    bool  selected    = isSelected(entity);
    bool  hadChildren = node->hasChildren();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    if (!hadChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // While searching, auto-expand any node that leads to a match.
    if (bSearching && subtreeMatchesFilter(node)) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    if (shouldAutoOpenForSelection(node)) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    if (bSelfMatch) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.75f, 1.0f, 1.0f));
    }
    bool   opened    = ImGui::TreeNodeEx((void*)(intptr_t)entity->getId(), flags, "%s", name.c_str());
    if (bSelfMatch) {
        ImGui::PopStyleColor();
    }
    ImVec2 itemMin   = ImGui::GetItemRectMin();
    ImVec2 itemMax   = ImGui::GetItemRectMax();
    bool   isHovered = ImGui::IsItemHovered();
    if (selected && _pendingScrollSelection == entity) {
        ImGui::SetScrollHereY(0.5f);
        _pendingScrollSelection = nullptr;
    }

    if (ImGui::IsItemClicked()) {
        handleEntityClick(entity);
    }
    // Right-click selects the item first so the context menu acts on the
    // whole selection when the item is already part of it.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !isSelected(entity)) {
        handleEntityClick(entity);
    }

    if (ImGui::BeginDragDropSource()) {
        Node* draggedNode = node;
        ImGui::SetDragDropPayload(NODE_DRAG_DROP_PAYLOAD, &draggedNode, sizeof(draggedNode));
        ImGui::TextUnformatted(name.c_str());
        ImGui::EndDragDropSource();
    }

    drawNodeDropTarget(node, itemMin, itemMax, isHovered);
    drawEntityNodeContextMenu(node, entity);

    if (opened && hadChildren) {
        for (Node* child : node->getChildren()) {
            drawNodeRecursive(child);
        }
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::drawFolderNode(Node* node)
{
    if (!node) {
        return;
    }

    const bool bSearching = isSearchActive();
    const bool bSelfMatch = bSearching && matchesFilter(getNodeName(node));
    if (bSearching && !bSelfMatch && !subtreeMatchesFilter(node)) {
        return;
    }

    const bool selected    = (node == _selectedFolder);
    const bool hadChildren = node->hasChildren();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hadChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (bSearching && subtreeMatchesFilter(node)) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.78f, 0.32f, 1.0f));
    bool opened = ImGui::TreeNodeEx((void*)(intptr_t)node, flags, "[Folder] %s", node->getName().c_str());
    ImGui::PopStyleColor();

    ImVec2 itemMin   = ImGui::GetItemRectMin();
    ImVec2 itemMax   = ImGui::GetItemRectMax();
    bool   isHovered = ImGui::IsItemHovered();

    if (ImGui::IsItemClicked()) {
        // Folder selection is organizational only: it replaces the entity
        // selection and never enters the entity selection bus.
        _selections.clear();
        _primarySelection = nullptr;
        _rangeAnchor      = nullptr;
        _selectedFolder   = node;
        notifyOwnerSelection();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && node != _selectedFolder) {
        _selectedFolder = node;
    }

    if (ImGui::BeginDragDropSource()) {
        Node* draggedNode = node;
        ImGui::SetDragDropPayload(NODE_DRAG_DROP_PAYLOAD, &draggedNode, sizeof(draggedNode));
        ImGui::TextUnformatted(node->getName().c_str());
        ImGui::EndDragDropSource();
    }

    drawNodeDropTarget(node, itemMin, itemMax, isHovered);
    drawFolderContextMenu(node);

    if (opened && hadChildren) {
        for (Node* child : node->getChildren()) {
            drawNodeRecursive(child);
        }
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::drawNodeDropTarget(Node* node, ImVec2 itemMin, ImVec2 itemMax, bool isHovered)
{
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
}

void SceneHierarchyPanel::drawCreateMenuItems(Node* parentNode)
{
    if (ImGui::MenuItem("Create Empty Node")) {
        if (Node* newNode = _context->createNode3D("New Node", parentNode)) {
            if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                setSelection(node3D->getEntity());
            }
        }
    }

    if (ImGui::BeginMenu("Create 3D Object")) {
        if (ImGui::MenuItem("Cube")) {
            Node* newNode = _context->createNode3D("Cube", parentNode);
            if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                Entity* newEntity = node3D->getEntity();
                auto    mc        = newEntity->addComponent<StaticMeshComponent>();
                mc->setPrimitiveGeometry(EPrimitiveGeometry::Cube);
                newEntity->addComponent<PhongMaterialComponent>();
                setSelection(newEntity);
            }
        }
        if (ImGui::MenuItem("Sphere")) {
            Node* newNode = _context->createNode3D("Sphere", parentNode);
            if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                Entity* newEntity = node3D->getEntity();
                auto    mc        = newEntity->addComponent<StaticMeshComponent>();
                mc->setPrimitiveGeometry(EPrimitiveGeometry::Sphere);
                newEntity->addComponent<PhongMaterialComponent>();
                setSelection(newEntity);
            }
        }
        if (ImGui::MenuItem("Plane")) {
            Node* newNode = _context->createNode3D("Plane", parentNode);
            if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                Entity* newEntity = node3D->getEntity();
                auto    mc        = newEntity->addComponent<StaticMeshComponent>();
                mc->setPrimitiveGeometry(EPrimitiveGeometry::Quad);
                newEntity->addComponent<PhongMaterialComponent>();
                setSelection(newEntity);
            }
        }
        if (ImGui::MenuItem("Terrain")) {
            Node* newNode = _context->createNode3D("Terrain", parentNode);
            if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                Entity* newEntity = node3D->getEntity();
                newEntity->addComponent<TerrainComponent>();
                newEntity->addComponent<PhongMaterialComponent>();
                setSelection(newEntity);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Create Point Light")) {
        Node* newNode = _context->createNode3D("Point Light", parentNode);
        if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
            Entity* newEntity = node3D->getEntity();
            newEntity->addComponent<PointLightComponent>();
            setSelection(newEntity);
        }
    }

    if (ImGui::MenuItem("Create Directional Light")) {
        Node* newNode = _context->createNode3D("Directional Light", parentNode);
        if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
            Entity* newEntity = node3D->getEntity();
            newEntity->addComponent<DirectionalLightComponent>();
            setSelection(newEntity);
        }
    }
}

void SceneHierarchyPanel::drawEntityNodeContextMenu(Node* node, Entity* entity)
{
    (void)node;
    ContextMenu ctx("NodeContextMenu##" + std::to_string(entity->getId()), ContextMenu::Type::EntityItem);
    if (ctx.begin()) {
        if (ctx.menuItem("Duplicate")) {
            duplicateSelection();
        }
        ctx.separator();
        if (ctx.menuItem("Delete")) {
            deleteSelection();
        }
        ctx.end();
    }
}

void SceneHierarchyPanel::drawFolderContextMenu(Node* folder)
{
    ContextMenu ctx("FolderContextMenu##" + std::to_string(reinterpret_cast<uintptr_t>(folder)),
                    ContextMenu::Type::EntityItem);
    if (ctx.begin()) {
        if (ctx.menuItem("Rename")) {
            _renamingFolder = folder;
            std::snprintf(_renameBuffer, sizeof(_renameBuffer), "%s", folder->getName().c_str());
            ImGui::OpenPopup("Rename Folder##rename");
        }
        ctx.separator();
        if (ctx.menuItem("Create Child Folder")) {
            _context->createFolder("New Folder", folder);
        }
        drawCreateMenuItems(folder);
        ctx.separator();
        if (ctx.menuItem("Duplicate")) {
            _pendingNodeDuplicate = {folder};
        }
        if (ctx.menuItem("Delete")) {
            _pendingFolderDelete = folder;
            ImGui::OpenPopup("Delete Folder##confirm");
        }
        ctx.end();
    }
}

void SceneHierarchyPanel::renderStandaloneEntities()
{
    auto view = _context->getRegistry().view<TransformComponent>();
    for (auto entityHandle : view) {
        Entity* entity = _context->getEntityByEnttID(entityHandle);
        if (entity && !_context->getNodeByEntity(entityHandle)) {
            if (isSearchActive() && !matchesFilter(entity->getName())) {
                continue;
            }
            drawFlatEntity(*entity);
        }
    }
}

void SceneHierarchyPanel::drawFlatEntity(Entity& entity)
{
    if (!entity) {
        return;
    }

    const char* name     = entity.getName().c_str();
    bool        selected = isSelected(&entity);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool opened = ImGui::TreeNodeEx((void*)(intptr_t)entity.getId(), flags, "%s", name);
    if (selected && _pendingScrollSelection == &entity) {
        ImGui::SetScrollHereY(0.5f);
        _pendingScrollSelection = nullptr;
    }

    if (ImGui::IsItemClicked()) {
        handleEntityClick(&entity);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !isSelected(&entity)) {
        handleEntityClick(&entity);
    }

    drawEntityNodeContextMenu(nullptr, &entity);

    if (opened) {
        ImGui::TreePop();
    }
}

Node* SceneHierarchyPanel::getSelectedNode() const
{
    if (_selectedFolder) {
        return _selectedFolder;
    }
    if (!_context || !_primarySelection) {
        return nullptr;
    }
    return _context->getNodeByEntity(_primarySelection);
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
    if (moved) {
        if (Entity* entity = draggedNode->getEntity()) {
            setSelection(entity);
        }
        else {
            _selectedFolder = draggedNode;
        }
    }
    return moved;
}

void SceneHierarchyPanel::duplicateSelection()
{
    _pendingNodeDuplicate.clear();
    for (Entity* entity : _selections) {
        if (!entity || !entity->isValid()) {
            continue;
        }
        if (Node* node = _context->getNodeByEntity(entity)) {
            _pendingNodeDuplicate.push_back(node);
        }
    }
}

void SceneHierarchyPanel::deleteSelection()
{
    _pendingEntityDelete = _selections;
    replaceSelection({}, nullptr);
}

void SceneHierarchyPanel::flushPendingActions()
{
    // Batch duplicate: deferred so node tree vectors are not mutated while a
    // parent's child list is being iterated during tree rendering.
    if (!_pendingNodeDuplicate.empty()) {
        std::vector<Entity*> duplicated;
        Node*                primaryFolder = nullptr;
        for (Node* node : _pendingNodeDuplicate) {
            if (!node) {
                continue;
            }
            Node* parent = node->getParent();
            if (Node* newNode = _context->duplicateNode(node, parent)) {
                if (newNode->isFolder()) {
                    if (!primaryFolder) {
                        primaryFolder = newNode;
                    }
                }
                else if (Entity* newEntity = newNode->getEntity()) {
                    duplicated.push_back(newEntity);
                }
            }
        }
        _pendingNodeDuplicate.clear();
        if (primaryFolder) {
            _selectedFolder = primaryFolder;
        }
        else if (!duplicated.empty()) {
            replaceSelection(duplicated, duplicated.front());
        }
    }

    // Batch delete: deferred so the tree render never touches destroyed
    // entities within the same frame.
    if (!_pendingEntityDelete.empty()) {
        for (Entity* entity : _pendingEntityDelete) {
            if (!entity || !entity->isValid() || entity->getScene() != _context) {
                continue;
            }
            _context->destroyEntity(entity);
        }
        _pendingEntityDelete.clear();
    }
}

void SceneHierarchyPanel::drawFolderRenamePopup()
{
    if (!_renamingFolder) {
        return;
    }
    if (ImGui::BeginPopupModal("Rename Folder##rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Rename folder");
        const bool bEnter = ImGui::InputText("Name",
                                             _renameBuffer,
                                             sizeof(_renameBuffer),
                                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::Button("Apply", ImVec2(120, 0)) || bEnter) {
            _renamingFolder->setName(_renameBuffer);
            _renamingFolder = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            _renamingFolder = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void SceneHierarchyPanel::drawFolderDeletePopup()
{
    if (!_pendingFolderDelete) {
        return;
    }
    if (ImGui::BeginPopupModal("Delete Folder##confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete folder '%s' and all its contents?", _pendingFolderDelete->getName().c_str());
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            Node* folder = _pendingFolderDelete;
            _pendingFolderDelete = nullptr;
            if (_selectedFolder == folder) {
                _selectedFolder = nullptr;
            }
            if (_renamingFolder == folder) {
                _renamingFolder = nullptr;
            }
            std::erase(_pendingNodeDuplicate, folder);
            if (_context) {
                _context->destroyNode(folder);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            _pendingFolderDelete = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

const std::string& SceneHierarchyPanel::getNodeName(Node* node) const
{
    // ★ 优先使用 Node 的名字（树状结构中的名字）
    return node->getName();
}

} // namespace ya
