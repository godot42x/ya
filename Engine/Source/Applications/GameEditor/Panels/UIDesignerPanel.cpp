#include "GameEditor/Panels/UIDesignerPanel.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include "GameEditor/EditorLayer.h"
#include "GameEditor/Inspector/TypeRenderer.h"

#include "GUI/Widgets/UITypeRegistry.h"

#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include "Scene/Core/Scene.h"

#include "GameRuntime/App.h"

#include <imgui.h>

#include <algorithm>

namespace ya
{

namespace
{

std::string shortTypeName(const std::string& typeId)
{
    const size_t dot = typeId.find_last_of('.');
    return dot == std::string::npos ? typeId : typeId.substr(dot + 1);
}

/// POD payload for designer widget-tree drag-drop (ImGui memcpy's it). The
/// pointer stays valid: the preview tree outlives the drag.
struct DesignerWidgetDragPayload
{
    UIElement* widget = nullptr;
};

constexpr const char* kDesignerWidgetDragPayloadName = "UI_DESIGNER_WIDGET_DRAG";

/// Find the strong reference to `widget` inside its parent's children.
UIElementRef refOf(UIElement* widget)
{
    if (!widget || !widget->getParent()) {
        return nullptr;
    }
    for (const auto& ref : widget->getParent()->getChildren()) {
        if (ref.get() == widget) {
            return ref;
        }
    }
    return nullptr;
}

void drawDesignerDropFeedback(const ImVec2& itemMin, const ImVec2& itemMax, UIDesignerPanel::EDropPos position)
{
    ImDrawList* drawList  = ImGui::GetWindowDrawList();
    const ImU32 lineColor = IM_COL32(80, 160, 255, 235);
    const ImU32 bandColor = IM_COL32(80, 160, 255, 70);
    const ImU32 fillColor = IM_COL32(80, 160, 255, 30);
    const float bandHeight = 8.0f;
    const float lineStartX = ImGui::GetCursorScreenPos().x - ImGui::GetTreeNodeToLabelSpacing();
    const float lineEndX   = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

    switch (position) {
    case UIDesignerPanel::EDropPos::Before:
        drawList->AddRectFilled({lineStartX, itemMin.y - bandHeight * 0.5f},
                                {lineEndX, itemMin.y + bandHeight * 0.5f}, bandColor, 2.0f);
        drawList->AddLine({lineStartX, itemMin.y}, {lineEndX, itemMin.y}, lineColor, 2.0f);
        ImGui::SetTooltip("插入到当前节点前");
        break;
    case UIDesignerPanel::EDropPos::Into:
        drawList->AddRectFilled({itemMin.x + 14.0f, itemMin.y + 4.0f},
                                {itemMax.x - 4.0f, itemMax.y - 4.0f}, fillColor, 3.0f);
        drawList->AddRect({itemMin.x + 14.0f, itemMin.y + 4.0f},
                          {itemMax.x - 4.0f, itemMax.y - 4.0f}, lineColor, 3.0f, 0, 1.5f);
        ImGui::SetTooltip("作为当前节点的子节点");
        break;
    case UIDesignerPanel::EDropPos::After:
        drawList->AddRectFilled({lineStartX, itemMax.y - bandHeight * 0.5f},
                                {lineEndX, itemMax.y + bandHeight * 0.5f}, bandColor, 2.0f);
        drawList->AddLine({lineStartX, itemMax.y}, {lineEndX, itemMax.y}, lineColor, 2.0f);
        ImGui::SetTooltip("插入到当前节点后");
        break;
    }
}

} // namespace

UIDesignerPanel::UIDesignerPanel(EditorLayer* owner) : _owner(owner)
{
}

UIDesignerPanel::~UIDesignerPanel() = default;

void UIDesignerPanel::openDocument(const std::shared_ptr<UIDocument>& document, const std::string& path)
{
    if (!document) {
        YA_CORE_WARN("UIDesignerPanel::openDocument: null document");
        return;
    }
    _document     = document;
    _documentPath = path;
    _entryScene   = nullptr;
    _entryId.clear();

    _previewTree  = std::make_unique<WidgetTree>(Extent2D{800, 600});
    _previewRoot  = document->instantiate();
    _selected     = nullptr;
    if (!_previewRoot) {
        YA_CORE_ERROR("UIDesignerPanel::openDocument: document '{}' failed to instantiate",
                      document->typeId);
        _document.reset();
        return;
    }
    _previewTree->attachToLayer(WidgetTree::ELayer::Content, _previewRoot);
    _selected = _previewRoot.get();
}

void UIDesignerPanel::openDocumentPath(const std::string& path)
{
    // Same resolve entry as the runtime (UIDocumentResolver): identical
    // schema/version/typeId rules for preview and PIE/runtime.
    if (auto document = _documentResolver.load(path)) {
        openDocument(document, path);
    }
}

void UIDesignerPanel::newDocument(const std::string& typeId)
{
    auto document     = std::make_shared<UIDocument>();
    document->typeId  = typeId;
    document->fields  = nlohmann::json::object();
    openDocument(document, {});
}

void UIDesignerPanel::openSceneEntry(Scene& scene, SceneWidgetEntry& entry)
{
    if (!entry.inlineDocument) {
        YA_CORE_WARN("UIDesignerPanel::openSceneEntry: entry '{}' has no inline document", entry.entryId);
        return;
    }
    _entryScene = &scene;
    _entryId    = entry.entryId;
    openDocument(entry.inlineDocument, {});
}

void UIDesignerPanel::rebuildDocumentFromPreview()
{
    if (!_previewRoot) {
        return;
    }
    _document = UIDocument::fromWidget(*_previewRoot);
}

bool UIDesignerPanel::saveDocument()
{
    if (!_document || !_previewRoot) {
        YA_CORE_WARN("UIDesignerPanel::saveDocument: no document open");
        return false;
    }
    rebuildDocumentFromPreview();

    // Scene-entry mode: write the rebuilt document back to the entry.
    if (_entryScene && !_entryId.empty()) {
        for (auto& entry : _entryScene->getWidgetEntries()) {
            if (entry.entryId == _entryId) {
                entry.inlineDocument = _document;
                YA_CORE_INFO("UIDesignerPanel: saved entry '{}' back to scene '{}'",
                             _entryId, _entryScene->getName());
                return true;
            }
        }
        YA_CORE_ERROR("UIDesignerPanel::saveDocument: entry '{}' no longer exists", _entryId);
        return false;
    }

    std::string path = _documentPath;
    if (path.empty()) {
        path = _savePathBuffer;
    }
    if (path.empty()) {
        YA_CORE_WARN("UIDesignerPanel::saveDocument: no save path (use Save As...)");
        return false;
    }
    if (!VirtualFileSystem::get()) {
        YA_CORE_ERROR("UIDesignerPanel::saveDocument: virtual file system unavailable");
        return false;
    }
    VirtualFileSystem::get()->saveToFile(path, _document->toJson().dump(4));
    _documentPath = path;
    // The runtime/PIE and hierarchy resolvers must re-read the updated file
    // (they cache by path; the designer held the live document).
    _documentResolver.invalidate(path);
    if (App* app = App::get(); app && app->getGameUIHost()) {
        app->getGameUIHost()->getDocumentResolver().invalidate(path);
    }
    YA_CORE_INFO("UIDesignerPanel: saved document to '{}'", path);
    return true;
}

UIFrameSnapshot UIDesignerPanel::buildPreviewSnapshot(const glm::vec2& uiScale, const glm::vec2& offset)
{
    if (!_previewTree) {
        return {};
    }
    UIFrameBuildContext ctx;
    ctx.uiScale = uiScale;
    ctx.offset  = offset;
    // Strong lifetime for the preview as well: the snapshot retains textures
    // until the editor canvas compose has recorded (shared resolver rules
    // with the runtime host).
    ctx.textureResolver = &resolveGameUITexture;
    return _previewTree->buildSnapshot(ctx);
}

UIElement* UIDesignerPanel::pickAt(const glm::vec2& logicalPoint)
{
    return _previewTree ? _previewTree->pickAt(logicalPoint) : nullptr;
}

const Rect2D* UIDesignerPanel::getSelectedLayoutRect() const
{
    if (!_selected || !_selected->isAttached() || _selected->getTree() != _previewTree.get()) {
        return nullptr;
    }
    return &_selected->_layoutRect;
}

void UIDesignerPanel::selectByChildPath(const std::vector<size_t>& path)
{
    if (!_previewRoot) {
        return;
    }
    UIElement* node = _previewRoot.get();
    for (const size_t index : path) {
        const auto& children = node->getChildren();
        if (index >= children.size()) {
            return;
        }
        node = children[index].get();
    }
    _selected = node;
}

void UIDesignerPanel::clearDocument()
{
    _document.reset();
    _documentPath.clear();
    _previewTree.reset();
    _previewRoot.reset();
    _selected   = nullptr;
    _entryScene = nullptr;
    _entryId.clear();
    endDrag();
}

void UIDesignerPanel::reloadCurrentDocument()
{
    if (_documentPath.empty() || !_document) {
        return;
    }
    _documentResolver.invalidate(_documentPath);
    openDocumentPath(_documentPath);
}

void UIDesignerPanel::syncPreviewToDocument()
{
    if (!_previewRoot) {
        return;
    }
    auto synced = UIDocument::fromWidget(*_previewRoot);
    if (!synced) {
        return;
    }
    _document = std::move(synced);

    // Inline scene-entry mode: write back to the entry so the Scene
    // Hierarchy's Game UI Entries tree reflects the edit immediately.
    // documentPath mode is picked up by the hierarchy through the
    // live-document path override (see drawWidgetEntryRow).
    if (_entryScene && !_entryId.empty()) {
        for (auto& entry : _entryScene->getWidgetEntries()) {
            if (entry.entryId == _entryId) {
                entry.inlineDocument = _document;
                break;
            }
        }
    }
}

UIDesignerPanel::EDropPos UIDesignerPanel::computeDropPos(float itemMinY, float itemMaxY)
{
    const float itemHeight      = std::max(itemMaxY - itemMinY, 1.0f);
    const float boundaryPadding = std::clamp(itemHeight * 0.33f, 8.0f, 14.0f);
    const float mouseY          = ImGui::GetMousePos().y;
    if (mouseY <= itemMinY + boundaryPadding) {
        return EDropPos::Before;
    }
    if (mouseY >= itemMaxY - boundaryPadding) {
        return EDropPos::After;
    }
    return EDropPos::Into;
}

void UIDesignerPanel::applyWidgetDrop(UIElement* dragged, UIElement& target, EDropPos position)
{
    if (!dragged || dragged == &target || !_previewTree) {
        return;
    }
    if (dragged == _previewRoot.get()) {
        return; // the document root keeps its place
    }
    if (!dragged->isAttached()) {
        return;
    }
    // Cycle guard: the target must not live inside the dragged subtree.
    for (UIElement* node = &target; node != nullptr; node = node->getParent()) {
        if (node == dragged) {
            YA_CORE_WARN("UIDesignerPanel: cannot drop into the dragged subtree");
            return;
        }
    }
    UIElementRef ref = refOf(dragged);
    if (!ref) {
        return;
    }
    switch (position) {
    case EDropPos::Before:
        _previewTree->reparentBefore(target, ref);
        break;
    case EDropPos::Into:
        _previewTree->reparent(target, ref);
        break;
    case EDropPos::After:
        _previewTree->reparentAfter(target, ref);
        break;
    }
    // The document must follow the preview, and the hierarchy must follow
    // the document (UMG-style live editing).
    syncPreviewToDocument();
}

void UIDesignerPanel::invalidatePreview()
{
    if (_previewTree) {
        _previewTree->invalidateLayout();
    }
}

bool UIDesignerPanel::deleteWidget(UIElement* widget)
{
    if (!widget || !widget->isAttached() || widget == _previewRoot.get()) {
        // The document root is the document: deleting it would orphan the
        // preview (and Save would rebuild an empty document).
        if (widget == _previewRoot.get()) {
            YA_CORE_WARN("UIDesignerPanel: cannot delete the document root");
        }
        return false;
    }
    WidgetTree* tree = widget->getTree();
    if (!tree) {
        return false;
    }
    tree->detach(*widget);
    if (_selected == widget) {
        _selected = nullptr;
    }
    syncPreviewToDocument();
    return true;
}

// === Canvas direct manipulation ===

namespace
{

/// Clamp a size to a sane minimum so resize drags cannot collapse a widget
/// to zero/inverted extent.
glm::vec2 clampMinSize(glm::vec2 size, float minExtent = 1.0f)
{
    size.x = std::max(size.x, minExtent);
    size.y = std::max(size.y, minExtent);
    return size;
}

} // namespace

void UIDesignerPanel::beginMove(UIElement* widget, const glm::vec2& canvasPoint)
{
    if (!widget || !widget->isAttached()) {
        return;
    }
    _dragMode        = EDragMode::Move;
    _dragWidget      = widget;
    _resizeMask      = 0;
    _dragStartPos    = widget->getPosition();
    _dragStartSize   = widget->getSize();
    _dragStartAnchorMin = widget->_anchorMin;
    _dragStartAnchorMax = widget->_anchorMax;
    _dragStartParentExtent = widget->getParent() ? widget->getParent()->_layoutRect.extent : glm::vec2(0.0f);
    _bDragMoved      = false;
    (void)canvasPoint;
}

void UIDesignerPanel::beginResize(UIElement* widget, const glm::vec2& canvasPoint, uint8_t resizeMask)
{
    if (!widget || !widget->isAttached()) {
        return;
    }
    _dragMode        = EDragMode::Resize;
    _dragWidget      = widget;
    _resizeMask      = resizeMask;
    _dragStartPos    = widget->getPosition();
    _dragStartSize   = widget->getSize();
    _dragStartAnchorMin = widget->_anchorMin;
    _dragStartAnchorMax = widget->_anchorMax;
    _dragStartParentExtent = widget->getParent() ? widget->getParent()->_layoutRect.extent : glm::vec2(0.0f);
    _bDragMoved      = false;
    (void)canvasPoint;
}

bool UIDesignerPanel::applyDragDelta(const glm::vec2& canvasDelta)
{
    if (_dragMode == EDragMode::None || !_dragWidget || !_dragWidget->isAttached()) {
        endDrag();
        return false;
    }
    if (!_bDragMoved) {
        if (glm::length(canvasDelta) < 3.0f) {
            return true; // click vs drag threshold: keep the session, no edit yet
        }
        _bDragMoved = true;
    }

    if (_dragMode == EDragMode::Move) {
        // _position is the offset from the anchor point inside the parent;
        // the parent rect does not move, so a canvas delta maps 1:1.
        _dragWidget->setPosition(_dragStartPos + canvasDelta);
    }
    else {
        UIElement* widget = _dragWidget;
        const float parentW = std::max(_dragStartParentExtent.x, 1.0f);
        const float parentH = std::max(_dragStartParentExtent.y, 1.0f);
        const bool bStretchX = _dragStartAnchorMax.x != _dragStartAnchorMin.x;
        const bool bStretchY = _dragStartAnchorMax.y != _dragStartAnchorMin.y;

        glm::vec2 pos       = _dragStartPos;
        glm::vec2 size      = _dragStartSize;
        glm::vec2 anchorMin = _dragStartAnchorMin;
        glm::vec2 anchorMax = _dragStartAnchorMax;

        const auto resizeMinEdge = [&](float& edgePos, float& edgeSize, float startPos, float startSize, float delta) {
            edgePos  = startPos + delta;
            edgeSize = startSize - delta;
            if (edgeSize < 1.0f) {
                // Never let the min edge cross the max edge: clamp size to the
                // minimum and pin the min edge so the max edge stays fixed.
                edgeSize = 1.0f;
                edgePos  = startPos + startSize - 1.0f;
            }
        };
        const auto resizeMaxEdge = [&](float& edgeSize, float startSize, float delta) {
            edgeSize = std::max(startSize + delta, 1.0f);
        };

        if (_resizeMask & kResizeHandleLeft) {
            if (bStretchX) {
                anchorMin.x = _dragStartAnchorMin.x + canvasDelta.x / parentW;
            }
            else {
                resizeMinEdge(pos.x, size.x, _dragStartPos.x, _dragStartSize.x, canvasDelta.x);
            }
        }
        if (_resizeMask & kResizeHandleRight) {
            if (bStretchX) {
                anchorMax.x = _dragStartAnchorMax.x + canvasDelta.x / parentW;
            }
            else {
                resizeMaxEdge(size.x, _dragStartSize.x, canvasDelta.x);
            }
        }
        if (_resizeMask & kResizeHandleTop) {
            if (bStretchY) {
                anchorMin.y = _dragStartAnchorMin.y + canvasDelta.y / parentH;
            }
            else {
                resizeMinEdge(pos.y, size.y, _dragStartPos.y, _dragStartSize.y, canvasDelta.y);
            }
        }
        if (_resizeMask & kResizeHandleBottom) {
            if (bStretchY) {
                anchorMax.y = _dragStartAnchorMax.y + canvasDelta.y / parentH;
            }
            else {
                resizeMaxEdge(size.y, _dragStartSize.y, canvasDelta.y);
            }
        }

        size      = clampMinSize(size);
        anchorMin = glm::clamp(anchorMin, 0.0f, 1.0f);
        anchorMax = glm::clamp(anchorMax, 0.0f, 1.0f);

        widget->setPosition(pos);
        widget->setSize(size);
        widget->_anchorMin  = anchorMin;
        widget->_anchorMax  = anchorMax;
    }

    invalidatePreview();
    return true;
}

void UIDesignerPanel::endDrag()
{
    _dragMode   = EDragMode::None;
    _dragWidget = nullptr;
    _resizeMask = 0;
    _bDragMoved = false;
}

void UIDesignerPanel::applyPreviewExtent()
{
    if (!_previewTree || !_owner) {
        return;
    }
    const glm::vec2 viewportSize = _owner->getViewportSize();
    if (viewportSize.x > 1.0f && viewportSize.y > 1.0f) {
        _previewTree->setLogicalExtent(Extent2D::fromVec2(viewportSize));
    }
}

void UIDesignerPanel::drawToolbar()
{
    // New: palette-driven root creation.
    if (ImGui::Button("New")) {
        ImGui::OpenPopup("UIDesignerNewType");
    }
    ImGui::SameLine();
    if (ImGui::Button("Open .yaui")) {
        _filePicker.open("Open UI Document", {}, {".yaui"}, [this](const std::string& path) {
            openDocumentPath(path);
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("Save") && hasDocument()) {
        saveDocument();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) {
        ImGui::OpenPopup("UIDesignerSaveAs");
    }

    if (ImGui::BeginPopup("UIDesignerNewType")) {
        for (const std::string& typeId : UITypeRegistry::instance().getTypeIds()) {
            if (ImGui::MenuItem(typeId.c_str())) {
                newDocument(typeId);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("UIDesignerSaveAs")) {
        ImGui::InputText("Path", _savePathBuffer, sizeof(_savePathBuffer));
        if (ImGui::Button("Save")) {
            if (std::strlen(_savePathBuffer) > 0) {
                _documentPath = _savePathBuffer;
                saveDocument();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    if (hasDocument()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s",
                            shortTypeName(_document->typeId).c_str(),
                            _documentPath.empty() ? " (untitled)" : "");
    }
}

void UIDesignerPanel::drawWidgetTree(UIElement& widget)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    const bool bSelected     = _selected == &widget;
    const bool bLeaf         = widget.getChildren().empty();
    if (bLeaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (bSelected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // Auto-expand the dragged-onto row so the drop target is visible.
    if (_dragHoverTarget == &widget) {
        ImGui::SetNextItemOpen(true);
    }
    const bool bOpened = ImGui::TreeNodeEx(&widget, flags, "%s  [%s]",
                                           widget._name.c_str(),
                                           shortTypeName(widget._typeId).c_str());
    if (ImGui::IsItemClicked()) {
        _selected = &widget;
    }
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete")) {
            deleteWidget(&widget);
        }
        ImGui::EndPopup();
    }

    // Drag the subtree (the document root keeps its place).
    if (&widget != _previewRoot.get()) {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            DesignerWidgetDragPayload payload;
            payload.widget = &widget;
            ImGui::SetDragDropPayload(kDesignerWidgetDragPayloadName, &payload, sizeof(payload));
            ImGui::Text("Reparent %s", widget._name.c_str());
            ImGui::EndDragDropSource();
        }
    }
    // Drop target: Before / Into / After by hover position.
    {
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        EDropPos     position = EDropPos::Into;
        bool         bHovered = false;
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDesignerWidgetDragPayloadName)) {
                const auto* src = static_cast<const DesignerWidgetDragPayload*>(payload->Data);
                position        = computeDropPos(itemMin.y, itemMax.y);
                bHovered        = true;
                _dragHoverTarget = &widget;
                if (payload->IsDelivery() && src->widget) {
                    applyWidgetDrop(src->widget, widget, position);
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (bHovered) {
            drawDesignerDropFeedback(itemMin, itemMax, position);
        }
    }

    if (bOpened && !bLeaf) {
        for (const auto& child : widget.getChildren()) {
            drawWidgetTree(*child);
        }
        ImGui::TreePop();
    }
}

void UIDesignerPanel::drawPalette()
{
    ImGui::SeparatorText("Palette");
    const bool bHasSelection = _selected != nullptr && _selected->isAttached();
    for (const std::string& typeId : UITypeRegistry::instance().getTypeIds()) {
        ImGui::PushID(typeId.c_str());
        const bool bAdd = ImGui::Button(shortTypeName(typeId).c_str(), ImVec2(-1.0f, 0.0f));
        if (bAdd) {
            UIElementRef widget = UITypeRegistry::instance().createInstance(typeId);
            if (widget && _previewTree) {
                widget->_name = shortTypeName(typeId);
                // UE/Godot semantics: adding always adds a CHILD under the
                // selection (or the document root when nothing is selected).
                // It never replaces or reparents the existing root.
                UIElement* parent = bHasSelection ? _selected : _previewRoot.get();
                if (parent) {
                    _previewTree->attach(*parent, widget);
                    _selected = widget.get();
                    // UMG-style live authoring: the document (and therefore
                    // the scene hierarchy's Game UI Entries tree) follows the
                    // preview immediately.
                    syncPreviewToDocument();
                }
                else {
                    newDocument(typeId);
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", typeId.c_str());
        }
        ImGui::PopID();
    }
}

void UIDesignerPanel::drawInspector()
{
    ImGui::SeparatorText("Inspector");
    if (!_selected || !_selected->isAttached()) {
        ImGui::TextDisabled("Select a widget");
        return;
    }

    auto* cls = ClassRegistry::instance().getClass(_selected->getTypeIndex());
    if (!cls) {
        ImGui::TextDisabled("Type not registered: %s", _selected->_typeId.c_str());
        return;
    }

    ya::RenderContext ctx;
    ctx.beginInstance(_selected);
    ya::renderReflectedType(cls->getName(), _selected->getTypeIndex(), _selected, ctx, 0);
    // Property edits must reach the canvas: layout caches the anchor math in
    // _layoutRect, so any reflected-field modification invalidates the tree.
    if (ctx.hasModifications()) {
        invalidatePreview();
    }
}

void UIDesignerPanel::onImGuiRender()
{
    if (!ImGui::Begin("UI Designer")) {
        ImGui::End();
        return;
    }

    // Clear the drag auto-expand when no designer-tree drag is active.
    if (!ImGui::GetDragDropPayload()) {
        _dragHoverTarget = nullptr;
    }

    drawToolbar();

    if (!hasDocument()) {
        ImGui::TextWrapped("Create a document with New (pick a widget type), open a .yaui "
                           "file, or open a SceneWidgetEntry from the Scene Hierarchy.");
        ImGui::End();
        _filePicker.render();
        return;
    }

    applyPreviewExtent();

    ImGui::SeparatorText("WidgetTree");
    if (_previewRoot) {
        drawWidgetTree(*_previewRoot);
    }
    else {
        ImGui::TextDisabled("Preview root missing");
    }

    drawPalette();
    drawInspector();

    ImGui::End();
    _filePicker.render();
}

} // namespace ya
