#include "GameEditor/Panels/SceneHierarchyPanel.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/Manager/Facade.h"
#include "GameEditor/EditorCommon.h"
#include "GameEditor/EditorLayer.h"


#include "Core/System/VirtualFileSystem.h"
#include "ECS/Component.h"
#include "ECS/Systems/Components/DirectionalLightComponent.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Systems/Components/PointLightComponent.h"
#include "ECS/Systems/Components/TerrainComponent.h"
#include "Scene3D/TransformComponent.h"
#include "GameEditor/Services/NodeCreateRegistry.h"
#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GameRuntime/App.h"
#include "Hierarchy/Node.h"
#include "Scene3D/Node3D.h"
#include "Scene/Core/Scene.h"
#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_internal.h> // ImRect / BeginDragDropTargetCustom


namespace ya
{

void SceneHierarchyPanel::onImGuiRender()
{
    YA_PROFILE_FUNCTION();
    sceneTree();
}

void SceneHierarchyPanel::setContext(Scene* scene)
{
    if (_context == scene) {
        return;
    }

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

    _pendingScrollSelection = nullptr;
    _pendingDraggedNode    = nullptr;
    _pendingDropTarget     = nullptr;
    _pendingNodeDuplicate.clear();
    _pendingEntityDelete.clear();
    _lastNodeOpenState.clear();
    _pendingTreeToggle.clear();
    _searchBuffer[0] = '\0';

    if (bSelectionChanged || !_primarySelection) {
        notifyOwnerSelection();
    }
}

void SceneHierarchyPanel::setSelection(Entity* entity)
{
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
        buildFlatEntityList();

        // === 3D section: the world node tree (same interactions as before). ===
        if (ImGui::TreeNodeEx("##SceneHierarchySection3D", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow, "3D (Scene)")) {
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

            // Render Node hierarchy tree (top-level list with the same insert-gap
            // drop targets as every sibling list; no layout space consumed).
            Node* rootNode = _context->getRootNode();
            if (rootNode && rootNode->hasChildren()) {
                const auto& children = rootNode->getChildren();
                drawNodeInsertGap(children.front(), ENodeDropPosition::Before, ImGui::GetCursorScreenPos().y);
                for (Node* child : children) {
                    drawNodeRecursive(child);
                }
            }

            // Blank-space drop target: reparent the dragged node to the root.
            ImGui::InvisibleButton("##SceneHierarchyRootDropTarget", ImVec2(std::max(ImGui::GetContentRegionAvail().x, 1.0f), 4.0f));
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
                    drawCreateMenuItems(parentNode);
                    ctx.end();
                }
            }
            ImGui::TreePop();
        }

        // === 2D section: Game UI nodes (references to .yaui documents). ===
        if (ImGui::TreeNodeEx("##SceneHierarchySection2D", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow, "2D (Game UI)")) {
            drawWidgetEntries();
            // Deferred document reparents (queued by the UI entry drag-drop)
            // mutate the scene only after the tree finished iterating.
            flushUIDrag();
            ImGui::TreePop();
        }
    }

    // Left-click on blank space to deselect
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        if (!ImGui::IsAnyItemHovered()) {
            replaceSelection({}, nullptr);
            _owner->setSelectedWidgetEntryId("");
        }
    }

    // Deferred batch actions and modals run after the tree render so node
    // mutation never happens while a parent's child list is being iterated.
    flushPendingActions();

    ImGui::End();
}

void SceneHierarchyPanel::drawWidgetEntries()
{
    // Keep the drag target open while a UI drag is active; after a drop keep
    // it open for a few frames so the result is visible.
    if (_uiPostDropExpandFrames > 0) {
        --_uiPostDropExpandFrames;
        _uiDragAutoExpand = _uiPostDropExpand;
    }
    else if (!ImGui::GetDragDropPayload()) {
        _uiDragAutoExpand.valid = false;
    }

    if (ImGui::SmallButton("+ Add")) {
        ImGui::OpenPopup("HierarchyAddUIEntry");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Top-level widgets mounted into the presented world's "
                        "WidgetTree on scene activation.");

    auto& entries = _context->getWidgetEntries();

    // The UI Designer's open document (not yet referenced by any scene entry
    // — e.g. a New/untitled document) is shown here so structural edits are
    // visible in the hierarchy immediately (UMG-style live authoring).
    {
        UIDesignerPanel& designer = _owner->getUIDesignerPanel();
        if (designer.hasDocument()) {
            const std::shared_ptr<UIDocument>& openDoc = designer.getOpenDocument();
            bool bReferenced = false;
            for (const auto& entry : entries) {
                if ((entry.inlineDocument && entry.inlineDocument == openDoc) ||
                    (!entry.documentPath.empty() && !designer.getDocumentPath().empty() &&
                     entry.documentPath == designer.getDocumentPath())) {
                    bReferenced = true;
                    break;
                }
            }
            if (!bReferenced) {
                ImGui::SeparatorText("Editing Document (untitled)");
                std::vector<size_t> childPath;
                drawLiveDocumentTree(openDoc, childPath);
            }
        }
    }

    if (entries.empty()) {
        ImGui::TextDisabled("(no entries)");
    }
    else {
        // Insert gap before the first entry (the row renderers add their own
        // After gaps at each row's subtree bottom; no layout space consumed).
        drawEntryInsertTarget(0, {}, ENodeDropPosition::Before, ImGui::GetCursorScreenPos().y);
        for (size_t i = 0; i < entries.size(); ++i) {
            drawWidgetEntryRow(entries[i], i);
        }
    }

    if (ImGui::BeginPopup("HierarchyAddUIEntry")) {
        drawAddEntryMenu();
        ImGui::EndPopup();
    }
}

namespace
{

std::string entryShortTypeName(const std::string& typeId)
{
    const size_t dot = typeId.find_last_of('.');
    return dot == std::string::npos ? typeId : typeId.substr(dot + 1);
}

/// Row drop feedback: a rounded highlight for a valid Into drop, a red one
/// for an invalid target (cycle / self / unresolvable).
void drawRowDropFeedback(const ImVec2& itemMin, const ImVec2& itemMax, bool bValid)
{
    ImDrawList* drawList  = ImGui::GetWindowDrawList();
    const ImU32 fillColor = bValid ? IM_COL32(80, 160, 255, 30) : IM_COL32(255, 80, 80, 30);
    const ImU32 lineColor = bValid ? IM_COL32(80, 160, 255, 235) : IM_COL32(255, 80, 80, 235);
    ImVec2      rectMin(itemMin.x + 14.0f, itemMin.y + 2.0f);
    ImVec2      rectMax(itemMax.x - 4.0f, itemMax.y - 2.0f);
    drawList->AddRectFilled(rectMin, rectMax, fillColor, 3.0f);
    drawList->AddRect(rectMin, rectMax, lineColor, 3.0f, 0, 1.5f);
    ImGui::SetTooltip(bValid ? "作为当前节点的子节点" : "不能作为子节点");
}

/// Shared 1px invisible insert-gap drop target between sibling tree rows —
/// the ONLY Before/After insert path. `payloadName` gates the payload;
/// `isValid` picks blue vs red feedback and gates delivery; `onDelivery`
/// queues the caller's move. Callers PushID a unique scope first.
/// Shared invisible insert-gap drop target: an implicit band around the
/// insertion line at `edgeY` (NO layout space is consumed — sibling row
/// spacing stays at the tree's natural padding). Uses the explicit custom
/// target so a ±grabHalf band around the line is hoverable (a 1px button was
/// too thin to notice), and draws a bright 2px highlight line + translucent
/// band while dragging over it (blue = valid, red = rejected).
void drawTreeInsertGapAt(float edgeY,
                         const char* payloadName,
                         SceneHierarchyPanel::ENodeDropPosition position,
                         const std::function<bool(const ImGuiPayload*)>& isValid,
                         const std::function<void(const ImGuiPayload*)>& onDelivery)
{
    constexpr float kGrabHalf = 4.0f; // hover band around the line (no layout impact)
    const float lineStartX = ImGui::GetCursorScreenPos().x - ImGui::GetTreeNodeToLabelSpacing();
    const float lineEndX   = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const ImRect gapRect({lineStartX, edgeY - kGrabHalf}, {lineEndX, edgeY + kGrabHalf});

    ImGuiID gapId = ImGui::GetID(position == SceneHierarchyPanel::ENodeDropPosition::After
                                     ? "##TreeGapAfter" : "##TreeGapBefore");
    if (gapId == 0) {
        gapId = 1; // BeginDragDropTargetCustom asserts non-zero ids
    }
    if (!ImGui::BeginDragDropTargetCustom(gapRect, gapId)) {
        return;
    }
    // AcceptBeforeDelivery: report the hover every frame while dragging so the
    // insertion highlight is visible BEFORE the mouse is released (default
    // AcceptDragDropPayload returns NULL until the delivery frame).
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
            payloadName,
            ImGuiDragDropFlags_AcceptNoDrawDefaultRect | ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        const bool bValid = !isValid || isValid(payload);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (bValid) {
            drawList->AddRectFilled({lineStartX, edgeY - 2.0f}, {lineEndX, edgeY + 2.0f},
                                    IM_COL32(80, 160, 255, 70), 1.0f);
            drawList->AddLine({lineStartX, edgeY}, {lineEndX, edgeY}, IM_COL32(80, 160, 255, 255), 2.0f);
        }
        else {
            drawList->AddRectFilled({lineStartX, edgeY - 2.0f}, {lineEndX, edgeY + 2.0f},
                                    IM_COL32(255, 80, 80, 70), 1.0f);
            drawList->AddLine({lineStartX, edgeY}, {lineEndX, edgeY}, IM_COL32(255, 80, 80, 255), 2.0f);
        }
        ImGui::SetTooltip(bValid
                              ? (position == SceneHierarchyPanel::ENodeDropPosition::After ? "插入到其后" : "插入到其前")
                              : "无法插入此处");
        if (payload->IsDelivery() && bValid && onDelivery) {
            onDelivery(payload);
        }
    }
    ImGui::EndDragDropTarget();
}

} // namespace

void SceneHierarchyPanel::drawWidgetEntryRow(SceneWidgetEntry& entry, size_t index)
{
    ImGui::PushID(static_cast<int>(index));

    const bool bSelected = _owner->getSelectedWidgetEntryId() == entry.entryId;
    std::shared_ptr<UIDocument> document = entry.inlineDocument;
    if (!document && !entry.documentPath.empty()) {
        if (App* app = App::get(); app && app->getGameUIHost()) {
            document = app->getGameUIHost()->getDocumentResolver().load(entry.documentPath);
        }
    }
    // UMG-style live mirror: when the UI Designer is editing this entry's
    // document, show the designer's live tree so palette adds / deletes /
    // drag-drops appear in the hierarchy immediately (no save required).
    if (UIDesignerPanel& designer = _owner->getUIDesignerPanel(); designer.hasDocument()) {
        if (entry.inlineDocument && designer.getOpenDocument() == entry.inlineDocument) {
            document = designer.getOpenDocument();
        }
        else if (!entry.documentPath.empty() && designer.getDocumentPath() == entry.documentPath) {
            document = designer.getOpenDocument();
        }
    }
    const bool        bLeaf   = !document || document->children.empty();
    const std::string typeId  = document ? document->typeId
                                         : (entry.documentPath.empty() ? "<invalid>" : entry.documentPath);

    char label[512];
    std::snprintf(label, sizeof(label), "%s  [%s]  z=%d%s",
                  entry.entryId.c_str(), entryShortTypeName(typeId).c_str(), entry.zOrder,
                  entry.autoMount ? "" : " (no auto)");
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (bLeaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (bSelected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // Auto-expand the dragged-onto entry so the drop target is visible.
    if (_uiDragAutoExpand.valid && _uiDragAutoExpand.entryIndex == index && _uiDragAutoExpand.path.empty()) {
        ImGui::SetNextItemOpen(true);
    }
    const bool bOpened = ImGui::TreeNodeEx(&entry, flags, "%s", label);
    // Capture the row rects IMMEDIATELY after the item (before any popup /
    // drag-source tooltip can change ImGui's last-item state).
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();

    if (ImGui::IsItemClicked()) {
        _owner->setSelectedWidgetEntryId(entry.entryId);
    }

    // Drag the entry root (whole document). Same pattern as the 3D node tree.
    publishUIDragSource(index, {});
    // Drop target: Into = become this entry's child. Registered for every
    // entry (inline and file-backed); file targets persist the .yaui.
    publishUIDragTarget(ImGui::GetID(&entry), index, {}, itemMin, itemMax);

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Open in UI Designer")) {
            openEntryWidgetInDesigner(entry, {});
        }
        if (ImGui::MenuItem("Delete Entry")) {
            _context->removeWidgetEntry(entry.entryId);
            if (_owner->getSelectedWidgetEntryId() == entry.entryId) {
                _owner->setSelectedWidgetEntryId("");
            }
        }
        ImGui::EndPopup();
    }

    // Leaf nodes (NoTreePushOnOpen) never push, so only recurse/pop when the
    // document actually has children (and the node is open).
    if (bOpened && document && !bLeaf) {
        std::vector<size_t> childPath;
        drawEntryDocumentTree(document, entry, index, childPath);
        ImGui::TreePop();
    }

    // Insert gap after this entry (and its whole subtree).
    drawEntryInsertTarget(index, {}, ENodeDropPosition::After, ImGui::GetCursorScreenPos().y);

    ImGui::PopID();
}

void SceneHierarchyPanel::drawEntryDocumentTree(const std::shared_ptr<UIDocument>& document,
                                                SceneWidgetEntry&                   entry,
                                                size_t                              entryIndex,
                                                std::vector<size_t>&                childPath)
{
    if (!document) {
        return;
    }
    const std::string name = document->fields.contains("_name") && document->fields["_name"].is_string()
                                 ? document->fields["_name"].get<std::string>()
                                 : entryShortTypeName(document->typeId);
    const bool bLeaf = document->children.empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (bLeaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    // Auto-expand the dragged-onto node so the drop target is visible.
    if (_uiDragAutoExpand.valid && _uiDragAutoExpand.entryIndex == entryIndex &&
        _uiDragAutoExpand.path == childPath) {
        ImGui::SetNextItemOpen(true);
    }
    const bool bOpened = ImGui::TreeNodeEx(document.get(), flags, "%s  [%s]",
                                           name.c_str(), entryShortTypeName(document->typeId).c_str());
    // Capture the row rects immediately after the item.
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    // Jump into the UI Designer document at this exact widget.
    if (ImGui::IsItemClicked()) {
        openEntryWidgetInDesigner(entry, childPath);
    }

    // Drag the document node subtree; drop supports Before/Into/After
    // (sibling reorder + nesting within the same document scope).
    publishUIDragSource(entryIndex, childPath);
    publishUIDragTarget(ImGui::GetID(document.get()), entryIndex, childPath, itemMin, itemMax);

    if (bOpened && !bLeaf) {
        // Insert gap before the first child (becomes the parent's first
        // child); each child row adds its own After gap at its subtree bottom.
        std::vector<size_t> gapPath = childPath;
        gapPath.push_back(0);
        drawEntryInsertTarget(entryIndex, gapPath, ENodeDropPosition::Before, ImGui::GetCursorScreenPos().y);
        for (size_t i = 0; i < document->children.size(); ++i) {
            childPath.push_back(i);
            drawEntryDocumentTree(document->children[i], entry, entryIndex, childPath);
            childPath.pop_back();
        }
        ImGui::TreePop();
    }
    // After this row (and its whole subtree): the next sibling's position.
    drawEntryInsertTarget(entryIndex, childPath, ENodeDropPosition::After, ImGui::GetCursorScreenPos().y);
}

void SceneHierarchyPanel::drawLiveDocumentTree(const std::shared_ptr<UIDocument>& document,
                                               std::vector<size_t>&               childPath)
{
    if (!document) {
        return;
    }
    const std::string name = document->fields.contains("_name") && document->fields["_name"].is_string()
                                 ? document->fields["_name"].get<std::string>()
                                 : entryShortTypeName(document->typeId);
    const bool bLeaf = document->children.empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (bLeaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    const bool bOpened = ImGui::TreeNodeEx(document.get(), flags, "%s  [%s]",
                                           name.c_str(), entryShortTypeName(document->typeId).c_str());
    if (ImGui::IsItemClicked()) {
        _owner->getUIDesignerPanel().selectByChildPath(childPath);
    }

    if (bOpened && !bLeaf) {
        for (size_t i = 0; i < document->children.size(); ++i) {
            childPath.push_back(i);
            drawLiveDocumentTree(document->children[i], childPath);
            childPath.pop_back();
        }
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::openEntryWidgetInDesigner(SceneWidgetEntry& entry, const std::vector<size_t>& childPath)
{
    UIDesignerPanel& designer = _owner->getUIDesignerPanel();
    if (entry.inlineDocument) {
        designer.openSceneEntry(*_context, entry);
    }
    else if (!entry.documentPath.empty()) {
        designer.openDocumentPath(entry.documentPath);
    }
    designer.selectByChildPath(childPath);
}

// === Game UI entry drag-drop (document-level parent-child editing) ===

std::shared_ptr<UIDocument> SceneHierarchyPanel::resolveEntryNode(const SceneWidgetEntry& entry,
                                                                  const std::vector<size_t>& path)
{
    std::shared_ptr<UIDocument> doc = entry.inlineDocument;
    for (const size_t index : path) {
        if (!doc || index >= doc->children.size()) {
            return nullptr;
        }
        doc = doc->children[index];
    }
    return doc;
}

void SceneHierarchyPanel::publishUIDragSource(size_t entryIndex, const std::vector<size_t>& path)
{
    // Same pattern as the proven 3D node tree drag source.
    if (!ImGui::BeginDragDropSource()) {
        return;
    }
    UIEntryDragPayload payload;
    payload.srcEntryIndex = entryIndex;
    payload.srcPathLength = static_cast<uint32_t>(
        std::min(path.size(), static_cast<size_t>(UI_DRAG_MAX_PATH_DEPTH)));
    for (uint32_t i = 0; i < payload.srcPathLength; ++i) {
        payload.srcPath[i] = static_cast<uint32_t>(path[i]);
    }
    ImGui::SetDragDropPayload(UI_ENTRY_DRAG_DROP_PAYLOAD, &payload, sizeof(payload));
    ImGui::Text("Reparent UI widget");
    ImGui::EndDragDropSource();
}

void SceneHierarchyPanel::publishUIDragTarget(ImGuiID                      rowId,
                                              size_t                     entryIndex,
                                              const std::vector<size_t>& path,
                                              const ImVec2&             itemMin,
                                              const ImVec2&             itemMax)
{
    // Into-only: drop on the row = become a child. Explicit row rect + the
    // row's own TreeNodeEx id (exactly like the 3D node rows, so the source
    // row is auto-rejected via id == SourceId). Non-zero required.
    if (rowId == 0) {
        rowId = 1; // BeginDragDropTargetCustom asserts non-zero ids
    }
    const ImRect rowRect(itemMin, itemMax);
    if (!ImGui::BeginDragDropTargetCustom(rowRect, rowId)) {
        return;
    }
    // AcceptBeforeDelivery: show the Into highlight while dragging (not just
    // on the release frame), same as the insert gaps.
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
            UI_ENTRY_DRAG_DROP_PAYLOAD,
            ImGuiDragDropFlags_AcceptNoDrawDefaultRect | ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        const auto* src = static_cast<const UIEntryDragPayload*>(payload->Data);
        // Auto-expand the hovered target (UMG/Godot-style).
        _uiDragAutoExpand = UIDragExpandTarget{.valid = true, .entryIndex = entryIndex, .path = path};
        const bool bValid = canAcceptUIEntryDrop(*src, entryIndex, path, ENodeDropPosition::Into);
        drawRowDropFeedback(itemMin, itemMax, bValid);
        if (payload->IsDelivery() && bValid) {
            queueUIDrag(*src, entryIndex, path, ENodeDropPosition::Into);
            YA_CORE_DEBUG("UI entry drop delivered: {} -> entry {} Into",
                          src->srcEntryIndex, entryIndex);
        }
    }
    ImGui::EndDragDropTarget();
}

void SceneHierarchyPanel::drawEntryInsertTarget(size_t                   entryIndex,
                                                const std::vector<size_t>& path,
                                                ENodeDropPosition         position,
                                                float                      edgeY)
{
    // Unique ID scope per (entry, path, position) — nested document gaps
    // share the entry index, so the child path must be part of the ID.
    std::string gapId = "gap_" + std::to_string(entryIndex) +
                        (position == ENodeDropPosition::After ? "_a" : "_b");
    for (const size_t p : path) {
        gapId += "_" + std::to_string(p);
    }
    ImGui::PushID(gapId.c_str());
    const ENodeDropPosition dropPos = position;
    drawTreeInsertGapAt(edgeY,
                        UI_ENTRY_DRAG_DROP_PAYLOAD,
                        dropPos,
                        [this, entryIndex, path, dropPos](const ImGuiPayload* payload) {
                            const auto* src = static_cast<const UIEntryDragPayload*>(payload->Data);
                            return canAcceptUIEntryDrop(*src, entryIndex, path, dropPos);
                        },
                        [this, entryIndex, path, dropPos](const ImGuiPayload* payload) {
                            const auto* src = static_cast<const UIEntryDragPayload*>(payload->Data);
                            queueUIDrag(*src, entryIndex, path, dropPos);
                            YA_CORE_DEBUG("UI entry insert delivered: {} -> entry {} {}",
                                          src->srcEntryIndex, entryIndex,
                                          dropPos == ENodeDropPosition::After ? "After" : "Before");
                        });
    ImGui::PopID();
}

void SceneHierarchyPanel::drawNodeInsertGap(Node* sibling, ENodeDropPosition position, float edgeY)
{
    ImGui::PushID(sibling);
    ImGui::PushID(position == ENodeDropPosition::After ? "GapA" : "GapB");
    drawTreeInsertGapAt(edgeY,
                        NODE_DRAG_DROP_PAYLOAD,
                        position,
                        [this, sibling](const ImGuiPayload* payload) {
                            Node* dragged = *static_cast<Node* const*>(payload->Data);
                            return canParentNode(dragged, sibling);
                        },
                        [this, sibling, position](const ImGuiPayload* payload) {
                            Node* dragged = *static_cast<Node* const*>(payload->Data);
                            queueMoveNode(dragged, sibling, position);
                            YA_CORE_DEBUG("Node insert delivered: {} {} {}", dragged->getName(),
                                          position == ENodeDropPosition::After ? "After" : "Before", sibling->getName());
                        });
    ImGui::PopID();
    ImGui::PopID();
}

bool SceneHierarchyPanel::canParentNode(Node* dragged, Node* target) const
{
    if (!_context || !dragged || !target || dragged == target) {
        return false;
    }
    if (dragged == _context->getRootNode()) {
        return false;
    }
    if (dragged->isAncestorOf(target)) {
        return false; // target lives inside the dragged subtree (cycle)
    }
    return true;
}

std::shared_ptr<UIDocument> SceneHierarchyPanel::resolveUIDocumentPath(const std::string& path) const
{
    UIDesignerPanel& designer = _owner->getUIDesignerPanel();
    if (designer.hasDocument() && designer.getDocumentPath() == path && designer.getOpenDocument()) {
        return designer.getOpenDocument();
    }
    if (App* app = App::get(); app && app->getGameUIHost()) {
        return app->getGameUIHost()->getDocumentResolver().load(path);
    }
    return nullptr;
}

bool SceneHierarchyPanel::canAcceptUIEntryDrop(const UIEntryDragPayload&  src,
                                               size_t                     dstEntryIndex,
                                               const std::vector<size_t>& dstPath,
                                               ENodeDropPosition          position) const
{
    auto& entries = _context->getWidgetEntries();
    std::vector<size_t> srcPath(src.srcPath, src.srcPath + src.srcPathLength);
    return canMoveWidgetEntryDocument(entries,
                                      src.srcEntryIndex,
                                      srcPath,
                                      dstEntryIndex,
                                      dstPath,
                                      static_cast<EWidgetEntryDropPosition>(position),
                                      [this](const std::string& file) { return resolveUIDocumentPath(file); });
}

void SceneHierarchyPanel::queueUIDrag(const UIEntryDragPayload&  payload,
                                      size_t                    dstEntryIndex,
                                      const std::vector<size_t>& dstPath,
                                      ENodeDropPosition         position)
{
    UIDragRequest req;
    req.srcEntryIndex = payload.srcEntryIndex;
    req.srcPath.assign(payload.srcPath, payload.srcPath + payload.srcPathLength);
    req.dstEntryIndex = dstEntryIndex;
    req.dstPath       = dstPath;
    req.position      = position;
    _pendingUIDrag        = std::move(req);
    _bHasPendingUIDrag    = true;
}

void SceneHierarchyPanel::flushUIDrag()
{
    if (!_bHasPendingUIDrag) {
        return;
    }
    UIDragRequest req = std::move(_pendingUIDrag);
    _bHasPendingUIDrag = false;

    auto& entries = _context->getWidgetEntries();
    if (req.srcEntryIndex >= entries.size() || req.dstEntryIndex >= entries.size()) {
        YA_CORE_WARN("SceneHierarchyPanel::flushUIDrag: stale drag indices");
        return;
    }

    // Resolve the documents before the move so the designer-refresh check
    // below can compare identity (shared_ptrs survive the mutation).
    std::shared_ptr<UIDocument> srcDoc =
        resolveEntryNode(entries[req.srcEntryIndex], req.srcPath);
    std::shared_ptr<UIDocument> dstDoc =
        resolveEntryNode(entries[req.dstEntryIndex], req.dstPath);
    if (!srcDoc || !dstDoc) {
        return;
    }

    const bool bSrcIsEntryRoot = req.srcPath.empty();
    const bool bDstIsEntryRoot = req.dstPath.empty();

    // documentPath entries participate through the shared host resolver; when
    // the UI Designer has the file open, its live document wins (unsaved edits
    // are the truth for the move and for the file persist below).
    std::vector<std::string> changedFiles;
    if (!moveWidgetEntryDocument(entries,
                                 req.srcEntryIndex,
                                 req.srcPath,
                                 req.dstEntryIndex,
                                 req.dstPath,
                                 static_cast<EWidgetEntryDropPosition>(req.position),
                                 [this](const std::string& file) { return resolveUIDocumentPath(file); },
                                 &changedFiles)) {
        return;
    }

    // Persist file-backed documents the move mutated and refresh caches.
    for (const std::string& file : changedFiles) {
        std::shared_ptr<UIDocument> doc = resolveUIDocumentPath(file);
        if (!doc || !VirtualFileSystem::get()) {
            continue;
        }
        VirtualFileSystem::get()->saveToFile(file, doc->toJson().dump(4));
        if (App* app = App::get(); app && app->getGameUIHost()) {
            app->getGameUIHost()->getDocumentResolver().invalidate(file);
        }
        // If the UI Designer has this .yaui open, re-open it from disk so its
        // preview and Save target the updated document.
        if (_owner->getUIDesignerPanel().getDocumentPath() == file) {
            _owner->getUIDesignerPanel().reloadCurrentDocument();
        }
    }

    // Keep the drop target open so the restructured tree is visible.
    {
        const size_t dstIdx = (bSrcIsEntryRoot && req.dstEntryIndex > req.srcEntryIndex)
                                  ? req.dstEntryIndex - 1
                                  : req.dstEntryIndex;
        _uiPostDropExpand      = UIDragExpandTarget{.valid = true, .entryIndex = dstIdx, .path = req.dstPath};
        _uiPostDropExpandFrames = 5;
    }

    // --- Keep the UI Designer consistent when it was editing one of the
    // moved documents (re-instantiate the destination entry's preview so a
    // later Save writes back the updated structure instead of a stale one) ---
    UIDesignerPanel& designer = _owner->getUIDesignerPanel();
    if (const std::shared_ptr<UIDocument>& openDoc = designer.getOpenDocument()) {
        if (openDoc == srcDoc || openDoc == dstDoc) {
            const size_t dstIdx = (bSrcIsEntryRoot && req.dstEntryIndex > req.srcEntryIndex)
                                      ? req.dstEntryIndex - 1
                                      : req.dstEntryIndex;
            if (dstIdx < entries.size() && entries[dstIdx].inlineDocument) {
                designer.openSceneEntry(*_context, entries[dstIdx]);
            }
            else {
                designer.clearDocument();
            }
        }
    }
}

void SceneHierarchyPanel::drawAddEntryMenu()
{
    const auto& typeIds = UITypeRegistry::instance().getTypeIds();
    if (typeIds.empty()) {
        ImGui::TextDisabled("No widget types registered");
        return;
    }

    const auto shortName = [](const std::string& typeId) {
        const size_t dot = typeId.find_last_of('.');
        return dot == std::string::npos ? typeId : typeId.substr(dot + 1);
    };

    for (const std::string& typeId : typeIds) {
        if (!ImGui::MenuItem(typeId.c_str())) {
            continue;
        }

        // UMG-style workflow: the main scene only REFERENCES .yaui files; the
        // file is created here (default document of the picked type) under the
        // project's Content/UI and the entry stores the reference path.
        std::string entryId = shortName(typeId);
        int         suffix  = 1;
        const auto& entries = _context->getWidgetEntries();
        const auto  bTaken  = [&](const std::string& id) {
            for (const auto& entry : entries) {
                if (entry.entryId == id) {
                    return true;
                }
            }
            return false;
        };
        while (bTaken(entryId) ||
               (VirtualFileSystem::get() && VirtualFileSystem::get()->isFileExists("Content/UI/" + entryId + ".yaui"))) {
            entryId = shortName(typeId) + "_" + std::to_string(suffix++);
        }

        auto document    = std::make_shared<UIDocument>();
        document->typeId = typeId;
        document->fields = nlohmann::json::object();
        const std::string vfsPath = "Content/UI/" + entryId + ".yaui";
        if (VirtualFileSystem::get()) {
            VirtualFileSystem::get()->saveToFile(vfsPath, document->toJson().dump(4));
        }

        SceneWidgetEntry entry;
        entry.entryId        = entryId;
        entry.documentPath   = vfsPath;
        entry.zOrder         = 0;
        entry.autoMount      = true;
        _context->addWidgetEntry(std::move(entry));
        _owner->setSelectedWidgetEntryId(entryId);
        ImGui::CloseCurrentPopup();
    }
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

    // While searching, auto-expand any node that leads to a match. Otherwise a
    // single click on a parent row toggles collapse/expand next frame (arrow
    // clicks are handled by ImGui itself).
    const bool bForceOpen = (bSearching && subtreeMatchesFilter(node)) || shouldAutoOpenForSelection(node);
    if (bForceOpen) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    else if (_pendingTreeToggle.erase(node) > 0) {
        const bool wasOpen = _lastNodeOpenState.contains(node) ? _lastNodeOpenState[node] : false;
        ImGui::SetNextItemOpen(!wasOpen, ImGuiCond_Always);
    }

    if (bSelfMatch) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.75f, 1.0f, 1.0f));
    }
    bool   opened    = ImGui::TreeNodeEx((void*)(intptr_t)entity->getId(), flags, "%s", name.c_str());
    if (bSelfMatch) {
        ImGui::PopStyleColor();
    }
    _lastNodeOpenState[node] = opened;
    ImVec2 itemMin = ImGui::GetItemRectMin();
    ImVec2 itemMax = ImGui::GetItemRectMax();
    if (selected && _pendingScrollSelection == entity) {
        ImGui::SetScrollHereY(0.5f);
        _pendingScrollSelection = nullptr;
    }

    if (ImGui::IsItemClicked()) {
        handleEntityClick(entity);
        if (hadChildren && !ImGui::IsItemToggledOpen()) {
            _pendingTreeToggle.insert(node);
        }
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

    drawNodeDropTarget(node, itemMin, itemMax);
    drawEntityNodeContextMenu(node, entity);

    if (opened && hadChildren) {
        // Insert gap before the first child; each child row adds its own
        // After gap at its subtree bottom. No layout space consumed.
        const auto& children = node->getChildren();
        drawNodeInsertGap(children.front(), ENodeDropPosition::Before, ImGui::GetCursorScreenPos().y);
        for (Node* child : children) {
            drawNodeRecursive(child);
        }
        ImGui::TreePop();
    }
    // Insert gap after this node (and its whole subtree).
    drawNodeInsertGap(node, ENodeDropPosition::After, ImGui::GetCursorScreenPos().y);
}

void SceneHierarchyPanel::drawNodeDropTarget(Node* node, ImVec2 itemMin, ImVec2 itemMax)
{
    // Into-only: drop on the node = become its child. Explicit row rect + a
    // stable non-zero id. BeginDragDropTargetCustom asserts id != 0, and the
    // entity id can legitimately be 0 (entt::null / freshly created entity),
    // so hash the node pointer instead (stable while the tree lives; the
    // source-row self-reject is handled by canParentNode below).
    const ImRect rowRect(itemMin, itemMax);
    ImGuiID rowId = ImGui::GetID(node);
    if (rowId == 0) {
        rowId = 1; // ImGui asserts non-zero ids
    }
    if (!ImGui::BeginDragDropTargetCustom(rowRect, rowId)) {
        return;
    }
    // AcceptBeforeDelivery: show the Into highlight while dragging (not just
    // on the release frame), same as the insert gaps.
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
            NODE_DRAG_DROP_PAYLOAD,
            ImGuiDragDropFlags_AcceptNoDrawDefaultRect | ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        Node* draggedNode = *static_cast<Node* const*>(payload->Data);
        const bool bValid = canParentNode(draggedNode, node);
        drawRowDropFeedback(itemMin, itemMax, bValid);
        if (payload->IsDelivery() && bValid) {
            queueMoveNode(draggedNode, node, ENodeDropPosition::Into);
            YA_CORE_DEBUG("Node drop delivered: {} Into {}", draggedNode->getName(), node->getName());
        }
    }
    ImGui::EndDragDropTarget();
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

    auto& createRegistry = editor::NodeCreateRegistry::get();

    // 3D presets grouped by category (registered centrally, not hardcoded here).
    std::string currentCategory;
    bool        bInMenu = false;
    for (const auto& entry : createRegistry.presets()) {
        if (entry.category != currentCategory) {
            if (bInMenu) {
                ImGui::EndMenu();
            }
            currentCategory = "Create " + entry.category;
            bInMenu         = ImGui::BeginMenu(currentCategory.c_str());
        }
        if (!bInMenu) {
            continue;
        }
        if (ImGui::MenuItem(entry.displayName.c_str())) {
            if (Node* newNode = entry.factory(*_context, entry.displayName, parentNode)) {
                if (Entity* newEntity = newNode->getEntity()) {
                    setSelection(newEntity);
                }
            }
        }
    }
    if (bInMenu) {
        ImGui::EndMenu();
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
    if (!_context || !_primarySelection) {
        return nullptr;
    }
    return _context->getNodeByEntity(_primarySelection);
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
        for (Node* node : _pendingNodeDuplicate) {
            if (!node) {
                continue;
            }
            Node* parent = node->getParent();
            if (Node* newNode = _context->duplicateNode(node, parent)) {
                if (Entity* newEntity = newNode->getEntity()) {
                    duplicated.push_back(newEntity);
                }
            }
        }
        _pendingNodeDuplicate.clear();
        if (!duplicated.empty()) {
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

const std::string& SceneHierarchyPanel::getNodeName(Node* node) const
{
    // ★ 优先使用 Node 的名字（树状结构中的名字）
    return node->getName();
}

} // namespace ya
