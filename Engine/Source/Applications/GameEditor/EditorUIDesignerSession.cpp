#include "GameEditor/EditorUIDesignerSession.h"

#include "Core/Log.h"

#include "GameEditor/EditorLayer.h"
#include "GameEditor/EditorUISlotEdit.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/UIDocumentStore.h"
#include "GUI/Widgets/UITypeRegistry.h"

#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include "Scene/Core/SceneWidgetEntry.h"

#include "GameRuntime/App.h"

#include <algorithm>
#include <cmath>

namespace ya
{

namespace
{

std::string shortTypeName(const std::string& typeId)
{
    const size_t dot = typeId.find_last_of('.');
    return dot == std::string::npos ? typeId : typeId.substr(dot + 1);
}

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

} // namespace

EditorUIDesignerSession::EditorUIDesignerSession(EditorLayer* owner) : _owner(owner)
{
}

EditorUIDesignerSession::~EditorUIDesignerSession()
{
    abandonDocument();
    *_lifetime = nullptr;
}

EditorDocumentRegistry* EditorUIDesignerSession::documents() const
{
    return _owner ? _owner->documentRegistry() : nullptr;
}

UIDocumentStore* EditorUIDesignerSession::documentStore() const
{
    return _owner ? _owner->uiDocumentStore() : nullptr;
}

void EditorUIDesignerSession::markDirty()
{
    if (_session) {
        _session->markDirty();
    }
}

void EditorUIDesignerSession::dropLocalDocument()
{
    cancelDrag();
    _document.reset();
    _previewTree.reset();
    _previewRoot.reset();
    _selected = nullptr;
    _documentPath.clear();
    _committedJson = nlohmann::json();
    _committedSelection.reset();
    _localUndo.clear();
    ++_previewGeneration;
}

UndoStack& EditorUIDesignerSession::undoStack()
{
    return _session ? _session->undo() : _localUndo;
}

void EditorUIDesignerSession::publishDocument()
{
    // The hierarchy and every mounted tree read the store, so an edit is
    // visible there before an explicit save writes the file.
    if (_documentPath.empty() || !_document) {
        return;
    }
    if (UIDocumentStore* store = documentStore()) {
        store->put(_documentPath, _document);
    }
}

std::optional<std::vector<size_t>> EditorUIDesignerSession::childPathOf(const UIElement* widget) const
{
    if (!widget || !_previewRoot) {
        return std::nullopt;
    }
    std::vector<size_t> path;
    for (const UIElement* node = widget; node != _previewRoot.get(); node = node->getParent()) {
        const UIElement* parent = node ? node->getParent() : nullptr;
        if (!parent) {
            return std::nullopt;
        }
        const auto& siblings = parent->getChildren();
        const auto  it       = std::find_if(siblings.begin(), siblings.end(), [node](const UIElementRef& child) {
            return child.get() == node;
        });
        if (it == siblings.end()) {
            return std::nullopt;
        }
        path.push_back(static_cast<size_t>(std::distance(siblings.begin(), it)));
    }
    std::reverse(path.begin(), path.end());
    return path;
}

void EditorUIDesignerSession::commitEdit(std::string label, std::string mergeKey)
{
    if (!_previewRoot) {
        return;
    }
    std::shared_ptr<UIDocument> edited = UIDocument::fromWidget(*_previewRoot);
    if (!edited) {
        YA_CORE_ERROR("EditorUIDesignerSession::commitEdit: '{}' produced no document", label);
        return;
    }
    auto after          = std::make_shared<const nlohmann::json>(edited->toJson());
    auto afterSelection = childPathOf(getSelectedWidget());
    if (*after == _committedJson) {
        _committedSelection = std::move(afterSelection);
        return;
    }
    auto before          = std::make_shared<const nlohmann::json>(std::move(_committedJson));
    auto beforeSelection = std::move(_committedSelection);

    // Snapshots, not widget pointers: restoring rebuilds the preview tree.
    const FEditorDocumentId documentId = _session ? _session->id() : FEditorDocumentId{};
    const std::weak_ptr<EditorUIDesignerSession*> lifetime = _lifetime;
    auto restore = [lifetime, documentId](const std::shared_ptr<const nlohmann::json>& snapshot,
                                          const std::optional<std::vector<size_t>>& selection) {
        const auto owner = lifetime.lock();
        EditorUIDesignerSession* self = owner ? *owner : nullptr;
        if (!self) {
            return;
        }
        const FEditorDocumentId openId = self->_session ? self->_session->id() : FEditorDocumentId{};
        if (openId != documentId) {
            YA_CORE_WARN("EditorUIDesignerSession: undo step belongs to a document that is no longer open");
            return;
        }
        self->restoreSnapshot(*snapshot, selection);
    };
    (void)undoStack().push({
        .label    = label,
        .mergeKey = std::move(mergeKey),
        .undo     = [restore, before, beforeSelection]() { restore(before, beforeSelection); },
        .redo     = [restore, after, afterSelection]() { restore(after, afterSelection); },
    });

    _document           = std::move(edited);
    _committedJson      = *after;
    _committedSelection = std::move(afterSelection);
    markDirty();
    publishDocument();
}

void EditorUIDesignerSession::restoreSnapshot(const nlohmann::json& snapshot,
                                              const std::optional<std::vector<size_t>>& selection)
{
    std::shared_ptr<UIDocument> document = UIDocument::fromJson(snapshot);
    if (!document) {
        YA_CORE_ERROR("EditorUIDesignerSession::restoreSnapshot: snapshot does not parse");
        return;
    }
    cancelDrag();
    if (!installPreview(document)) {
        return;
    }
    _selected           = selection ? findByChildPath(*selection) : nullptr;
    _committedSelection = childPathOf(_selected);
    markDirty();
    publishDocument();
}

bool EditorUIDesignerSession::closeSession(EEditorDocumentCloseMode mode)
{
    if (!_session) {
        dropLocalDocument();
        return true;
    }
    EditorDocumentRegistry* docs = documents();
    const FEditorDocumentId id   = _session->id();
    if (!docs) {
        _session = nullptr;
        dropLocalDocument();
        return true;
    }
    _session->releaseBind();
    if (_session->bindCount() > 0) {
        _session = nullptr;
        dropLocalDocument();
        return true;
    }
    const EEditorDocumentCloseResult result = docs->close(id, mode);
    if (result != EEditorDocumentCloseResult::Closed) {
        _session->addBind();
        return false;
    }
    _session = nullptr;
    dropLocalDocument();
    return true;
}

bool EditorUIDesignerSession::adoptSession(const FEditorDocumentId& id)
{
    if (_session && _session->id() == id) {
        return true;
    }
    (void)closeSession(EEditorDocumentCloseMode::Discard);
    EditorDocumentRegistry* docs = documents();
    if (!docs || !id.valid()) {
        return true;
    }
    _session = docs->open(id, EEditorDocumentClosePolicy::RejectIfDirty);
    if (!_session) {
        return false;
    }
    _session->addBind();
    (void)docs->claimPreview(id);
    return true;
}

bool EditorUIDesignerSession::closeDocument()
{
    return closeSession(EEditorDocumentCloseMode::Request);
}

void EditorUIDesignerSession::clearDocument()
{
    (void)closeSession(EEditorDocumentCloseMode::Discard);
}

void EditorUIDesignerSession::abandonDocument()
{
    (void)closeSession(EEditorDocumentCloseMode::Force);
}

void EditorUIDesignerSession::openDocument(std::string_view path)
{
    if (path.empty()) {
        YA_CORE_WARN("EditorUIDesignerSession::openDocument: empty document path");
        return;
    }
    UIDocumentStore* store = documentStore();
    if (!store) {
        YA_CORE_ERROR("EditorUIDesignerSession::openDocument: no document store bound; "
                      "cannot open '{}'",
                      path);
        return;
    }
    const std::shared_ptr<UIDocument> document = store->resolve(path);
    if (!document) {
        return;
    }
    if (!adoptSession(makeEditorUIDocumentId(path))) {
        return;
    }
    if (!installPreview(document)) {
        (void)closeSession(EEditorDocumentCloseMode::Force);
        return;
    }
    _canvas.bFitPending = true;
    _documentPath = std::string(path);
    if (_owner) {
        _owner->showUIDesignerCanvas();
    }
}

void EditorUIDesignerSession::openUntitled(const std::shared_ptr<UIDocument>& document)
{
    EditorDocumentRegistry* docs = documents();
    const FEditorDocumentId id =
        makeEditorUIDocumentId(docs ? docs->makeUntitledKey() : std::string("local"));
    if (!adoptSession(id)) {
        return;
    }
    if (!installPreview(document)) {
        (void)closeSession(EEditorDocumentCloseMode::Force);
        return;
    }
    _canvas.bFitPending = true;
    _documentPath.clear();
    if (_owner) {
        _owner->showUIDesignerCanvas();
    }
}

bool EditorUIDesignerSession::installPreview(const std::shared_ptr<UIDocument>& document)
{
    _document     = document;
    _previewTree  = std::make_unique<WidgetTree>(Extent2D{_designResolution.x, _designResolution.y});
    _previewTree->setTextureSource(&gameUITextureSource());
    _previewRoot  = document->instantiate();
    _selected     = nullptr;
    if (!_previewRoot) {
        YA_CORE_ERROR("EditorUIDesignerSession::openDocument: document '{}' failed to instantiate",
                      document->typeId);
        _document.reset();
        _previewTree.reset();
        return false;
    }
    FCanvasSlotArgs fillArgs;
    fillArgs.anchorMin = {0.0f, 0.0f};
    fillArgs.anchorMax = {1.0f, 1.0f};
    const WidgetAttachment attachment = _previewTree->attachToLayer(WidgetTree::ELayer::Content,
                                                                     _previewRoot,
                                                                     fillArgs);
    YA_CORE_ASSERT(attachment.valid(), "EditorUIDesignerSession: failed to attach preview root");
    _selected = _previewRoot.get();
    ++_previewGeneration;
    // The baseline is the preview's own round trip, so an untouched preview
    // never commits a spurious edit against the stored form.
    const std::shared_ptr<UIDocument> baseline = UIDocument::fromWidget(*_previewRoot);
    _committedJson      = baseline ? baseline->toJson() : nlohmann::json();
    _committedSelection = childPathOf(_selected);
    return true;
}

void EditorUIDesignerSession::setDesignResolution(glm::uvec2 size)
{
    size = glm::max(size, glm::uvec2(16));
    if (size == _designResolution) {
        return;
    }
    _designResolution = size;
    if (_previewTree) {
        _previewTree->setLogicalExtent(Extent2D{size.x, size.y});
    }
    _canvas.bFitPending = true;
}

void EditorUIDesignerSession::newDocument(const std::string& typeId)
{
    auto document     = std::make_shared<UIDocument>();
    document->typeId  = typeId;
    document->fields  = nlohmann::json::object();
    openUntitled(document);
}

void EditorUIDesignerSession::openSceneEntry(const SceneWidgetEntry& entry)
{
    if (entry.documentPath.empty()) {
        YA_CORE_WARN("EditorUIDesignerSession::openSceneEntry: entry '{}' has no document path",
                     entry.entryId);
        return;
    }
    openDocument(entry.documentPath);
}

void EditorUIDesignerSession::rebuildDocumentFromPreview()
{
    if (!_previewRoot) {
        return;
    }
    _document = UIDocument::fromWidget(*_previewRoot);
}

bool EditorUIDesignerSession::saveDocument()
{
    if (!_document || !_previewRoot) {
        YA_CORE_WARN("EditorUIDesignerSession::saveDocument: no document open");
        return false;
    }
    rebuildDocumentFromPreview();

    if (!_documentPath.empty()) {
        UIDocumentStore* store = documentStore();
        if (!store) {
            YA_CORE_ERROR("EditorUIDesignerSession::saveDocument: no document store bound; "
                          "cannot save '{}'",
                          _documentPath);
            return false;
        }
        // Publish first: the hierarchy and every mounted tree read the store, so
        // an edit is visible even if the file write fails.
        store->put(_documentPath, _document);
        if (!store->save(_documentPath)) {
            return false;
        }
    }
    else {
        // Untitled: the rebuilt document stays in `_document`. There is no
        // save target until the asset is created (see the UI asset browser).
        YA_CORE_INFO("EditorUIDesignerSession: rebuilt untitled document '{}'", _document->typeId);
    }

    if (_session) {
        _session->clearDirty();
    }
    return true;
}

UIFrameSnapshot EditorUIDesignerSession::buildPreviewSnapshot(const glm::vec2& uiScale, const glm::vec2& offset)
{
    if (!_previewTree) {
        return {};
    }
    UIFrameBuildContext ctx;
    ctx.uiScale = uiScale;
    ctx.offset  = offset;
    // Authoring canvas: a document whose root (or any subtree) ships Hidden is
    // shown dimmed, not blanked -- the runtime hides it, the designer edits it.
    ctx.ghostInvisibleOpacity = kDesignerGhostOpacity;
    // Strong lifetime for the preview as well: the snapshot retains textures
    // until the editor canvas compose has recorded (shared resolver rules
    // with the runtime host).
    ctx.textureResolver = &resolveGameUITexture;
    return _previewTree->buildSnapshot(ctx);
}

UIElement* EditorUIDesignerSession::pickAt(const glm::vec2& logicalPoint)
{
    return _previewTree ? _previewTree->pickAt(logicalPoint) : nullptr;
}

const Rect2D* EditorUIDesignerSession::getSelectedLayoutRect() const
{
    if (!_selected || !_selected->isAttached() || _selected->getTree() != _previewTree.get()) {
        return nullptr;
    }
    return &_selected->_layoutRect;
}

UIElement* EditorUIDesignerSession::findByChildPath(const std::vector<size_t>& path) const
{
    if (!_previewRoot) {
        return nullptr;
    }
    UIElement* node = _previewRoot.get();
    for (const size_t index : path) {
        const auto& children = node->getChildren();
        if (index >= children.size()) {
            return nullptr;
        }
        node = children[index].get();
    }
    return node;
}

void EditorUIDesignerSession::select(UIElement* widget)
{
    _selected           = widget;
    _committedSelection = childPathOf(widget);
}

void EditorUIDesignerSession::selectByChildPath(const std::vector<size_t>& path)
{
    select(findByChildPath(path));
}

EditorUIDesignerSession::EDropPos EditorUIDesignerSession::computeDropPos(float itemMinY, float itemMaxY, float mouseY)
{
    const float itemHeight      = std::max(itemMaxY - itemMinY, 1.0f);
    const float boundaryPadding = std::clamp(itemHeight * 0.33f, 8.0f, 14.0f);
    if (mouseY <= itemMinY + boundaryPadding) {
        return EDropPos::Before;
    }
    if (mouseY >= itemMaxY - boundaryPadding) {
        return EDropPos::After;
    }
    return EDropPos::Into;
}

void EditorUIDesignerSession::applyWidgetDrop(UIElement* dragged, UIElement& target, EDropPos position)
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
            YA_CORE_WARN("EditorUIDesignerSession: cannot drop into the dragged subtree");
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
    commitEdit("Move " + dragged->_name);
}

std::string EditorUIDesignerSession::paletteDisplayName(const std::string& typeId)
{
    return shortTypeName(typeId);
}

bool EditorUIDesignerSession::addPaletteWidget(const std::string& typeId)
{
    if (!_previewTree || !_previewRoot) {
        newDocument(typeId);
        return hasDocument();
    }

    UIElementRef widget = UITypeRegistry::instance().createInstance(typeId);
    if (!widget) {
        return false;
    }
    widget->_name = shortTypeName(typeId);

    UIElement* target = (_selected && _selected->isAttached()) ? _selected : _previewRoot.get();
    if (!target) {
        return false;
    }
    if (target->getLayout()) {
        _previewTree->attach(*target, widget);
    }
    else if (target != _previewRoot.get() && target->getParent()) {
        _previewTree->attach(*target->getParent(), widget);
        _previewTree->reparentAfter(*target, widget);
    }
    else {
        YA_CORE_WARN("EditorUIDesignerSession: document root '{}' has no layout and cannot hold '{}'",
                     target->_name,
                     typeId);
        return false;
    }
    _selected = widget.get();
    commitEdit("Add " + widget->_name);
    return true;
}

UIElement* EditorUIDesignerSession::duplicateWidget(UIElement* widget)
{
    if (!widget || widget == _previewRoot.get() || !childPathOf(widget) || !_previewTree) {
        return nullptr;
    }
    UIElement*                        parent   = widget->getParent();
    const UISlot*                     slot     = parent->getSlotForChild(*widget);
    const std::shared_ptr<UIDocument> document = UIDocument::fromWidget(*widget);
    UIElementRef                      copy     = document ? document->instantiate() : nullptr;
    if (!copy || !slot) {
        YA_CORE_ERROR("EditorUIDesignerSession: failed to duplicate '{}'", widget->_name);
        return nullptr;
    }
    nlohmann::json slotState;
    slot->serialize(slotState);
    copy->_name = widget->_name + "_copy";
    _previewTree->attach(*parent, copy);
    // Same parent, so the reorder keeps the copied slot.
    parent->getSlotForChild(*copy)->deserialize(slotState);
    _previewTree->reparentAfter(*widget, copy);
    _selected = copy.get();
    commitEdit("Duplicate " + widget->_name);
    return copy.get();
}

void EditorUIDesignerSession::invalidatePreview()
{
    if (_previewTree) {
        _previewTree->invalidateLayout();
    }
}

bool EditorUIDesignerSession::deleteWidget(UIElement* widget)
{
    if (!widget || !widget->isAttached() || widget == _previewRoot.get()) {
        // The document root is the document: deleting it would orphan the
        // preview (and Save would rebuild an empty document).
        if (widget == _previewRoot.get()) {
            YA_CORE_WARN("EditorUIDesignerSession: cannot delete the document root");
        }
        return false;
    }
    WidgetTree* tree = widget->getTree();
    if (!tree) {
        return false;
    }
    const std::string label = "Delete " + widget->_name;
    tree->detach(*widget);
    if (_selected == widget) {
        _selected = nullptr;
    }
    commitEdit(label);
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

/// Read a widget's current canvas-intent geometry. Direct manipulation is
/// defined only for a parent-owned canvas edge; other layout hosts must be
/// edited through their typed slot controls.
bool readCanvasIntent(const UIElement& widget,
                      glm::vec2&       outPos,
                      glm::vec2&       outSize,
                      glm::vec2&       outMin,
                      glm::vec2&       outMax)
{
    if (const UIElement* parent = widget.getParent()) {
        if (const auto* slot = dynamic_cast<const UICanvasSlot*>(parent->getSlotForChild(widget))) {
            outPos = slot->getOffset();
            outSize = slot->getFixedSize();
            outMin = slot->getAnchorMin();
            outMax = slot->getAnchorMax();
            return true;
        }
    }
    return false;
}

} // namespace

void EditorUIDesignerSession::beginMove(UIElement* widget, const glm::vec2& canvasPoint)
{
    if (!widget || !widget->isAttached()) {
        return;
    }
    _dragMode        = EDragMode::Move;
    _dragWidget      = widget;
    _resizeMask      = 0;
    if (!readCanvasIntent(*widget, _dragStartPos, _dragStartSize, _dragStartAnchorMin, _dragStartAnchorMax)) {
        YA_CORE_WARN("EditorUIDesignerSession::beginMove: widget '{}' is not attached through a canvas slot",
                     widget->_name);
        _dragMode = EDragMode::None;
        _dragWidget = nullptr;
        return;
    }
    _dragStartParentExtent = widget->getParent() ? widget->getParent()->_layoutRect.extent : glm::vec2(0.0f);
    _bDragMoved      = false;
    (void)canvasPoint;
}

void EditorUIDesignerSession::beginResize(UIElement* widget, const glm::vec2& canvasPoint, uint8_t resizeMask)
{
    if (!widget || !widget->isAttached()) {
        return;
    }
    _dragMode        = EDragMode::Resize;
    _dragWidget      = widget;
    _resizeMask      = resizeMask;
    if (!readCanvasIntent(*widget, _dragStartPos, _dragStartSize, _dragStartAnchorMin, _dragStartAnchorMax)) {
        YA_CORE_WARN("EditorUIDesignerSession::beginResize: widget '{}' is not attached through a canvas slot",
                     widget->_name);
        _dragMode = EDragMode::None;
        _dragWidget = nullptr;
        return;
    }
    _dragStartParentExtent = widget->getParent() ? widget->getParent()->_layoutRect.extent : glm::vec2(0.0f);
    _bDragMoved      = false;
    (void)canvasPoint;
}

bool EditorUIDesignerSession::applyDragDelta(const glm::vec2& canvasDelta)
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
        if (UIElement* parent = _dragWidget->getParent()) {
            if (auto* slot = dynamic_cast<UICanvasSlot*>(parent->getSlotForChild(*_dragWidget))) {
                slot->setOffset(snapOffset(_dragStartPos + canvasDelta));
                invalidatePreview();
                return true;
            }
        }
        YA_CORE_ERROR("EditorUIDesignerSession::applyDragDelta: move lost its parent canvas slot");
        endDrag();
        return false;
    }
    else {
        const float parentW = std::max(_dragStartParentExtent.x, 1.0f);
        const float parentH = std::max(_dragStartParentExtent.y, 1.0f);
        const bool bStretchX = _dragStartAnchorMax.x != _dragStartAnchorMin.x;
        const bool bStretchY = _dragStartAnchorMax.y != _dragStartAnchorMin.y;

        glm::vec2 pos       = _dragStartPos;
        glm::vec2 size      = _dragStartSize;
        glm::vec2 anchorMin = _dragStartAnchorMin;
        glm::vec2 anchorMax = _dragStartAnchorMax;

        // Point-anchor resize snaps the moving edges to the grid; anchored
        // (stretch) edges are fractional by definition and never snap.
        const auto resizeMinEdge = [&](float& edgePos, float& edgeSize, float startPos, float startSize, float delta) {
            edgePos  = snapAxis(startPos + delta);
            edgeSize = startSize - (edgePos - startPos);
            if (edgeSize < 1.0f) {
                // Never let the min edge cross the max edge: clamp size to the
                // minimum and pin the min edge so the max edge stays fixed.
                edgeSize = 1.0f;
                edgePos  = startPos + startSize - 1.0f;
            }
        };
        const auto resizeMaxEdge = [&](float& edgeSize, float startSize, float delta) {
            edgeSize = std::max(snapAxis(startSize + delta), 1.0f);
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

        // Layout intent lives on the parent->child slot edge. When the parent is
        // a canvas host the anchors must be written to that slot, otherwise the
        // host arranges from the default slot and the drag result is lost.
        if (UIElement* parent = _dragWidget->getParent()) {
            if (UISlot* slot = parent->getSlotForChild(*_dragWidget)) {
                FCanvasSlotArgs args;
                args.anchorMin = anchorMin;
                args.anchorMax = anchorMax;
                args.offset    = pos;
                args.fixedSize = size;
                if (slot->applyArgs(args)) {
                    invalidatePreview();
                    return true;
                }
            }
        }

        YA_CORE_ERROR("EditorUIDesignerSession::applyDragDelta: canvas drag lost its parent canvas slot");
        endDrag();
        return false;
    }

    invalidatePreview();
    return true;
}

std::unique_ptr<EditorUISlotEdit> EditorUIDesignerSession::editSlot(UIElement* widget) const
{
    if (!widget || widget == _previewRoot.get() || !childPathOf(widget)) {
        return nullptr;
    }
    return EditorUISlotEdit::forChild(*widget);
}

void EditorUIDesignerSession::setSnapToGrid(const bool enabled)
{
    _bSnapToGrid = enabled;
}

bool EditorUIDesignerSession::nudgeSelection(const glm::vec2& canvasDelta)
{
    UIElement* selected = getSelectedWidget();
    if (!selected || selected == _previewRoot.get() || !childPathOf(selected)) {
        return false;
    }
    UIElement* parent = selected->getParent();
    auto*      slot   = parent ? dynamic_cast<UICanvasSlot*>(parent->getSlotForChild(*selected)) : nullptr;
    if (!slot) {
        return false;
    }
    // Nudging is fine adjustment: exact pixels, never quantized to the grid.
    slot->setOffset(slot->toArgs().offset + canvasDelta);
    invalidatePreview();
    commitEdit("Nudge " + selected->_name, "nudge");
    return true;
}

bool EditorUIDesignerSession::applyCanvasAnchorPreset(UIElement* widget, ECanvasAnchorPreset preset)
{
    if (!widget || widget == _previewRoot.get() || !childPathOf(widget)) {
        return false;
    }
    UIElement* parent = widget->getParent();
    auto*      slot   = parent ? dynamic_cast<UICanvasSlot*>(parent->getSlotForChild(*widget)) : nullptr;
    if (!slot) {
        return false;
    }
    const FCanvasSlotArgs current = slot->toArgs();
    const glm::vec2       size    = widget->_layoutRect.extent;
    slot->assign(withCanvasAnchorPreset(current, preset, size.x > 0.0f && size.y > 0.0f ? size : current.fixedSize));
    invalidatePreview();
    commitEdit("Anchor " + widget->_name);
    return true;
}

void EditorUIDesignerSession::cancelDrag()
{
    _dragMode   = EDragMode::None;
    _dragWidget = nullptr;
    _resizeMask = 0;
    _bDragMoved = false;
}

void EditorUIDesignerSession::endDrag()
{
    const bool      moved = _bDragMoved;
    const EDragMode mode  = _dragMode;
    UIElement*      widget = _dragWidget;
    _dragMode   = EDragMode::None;
    _dragWidget = nullptr;
    _resizeMask = 0;
    _bDragMoved = false;
    if (moved && widget) {
        commitEdit((mode == EDragMode::Resize ? "Resize " : "Move ") + widget->_name);
    }
}

uint8_t EditorUIDesignerSession::hitTestResizeHandles(const glm::vec2& viewPoint) const
{
    const Rect2D* selected = getSelectedLayoutRect();
    if (!selected) {
        return 0;
    }
    const glm::vec2 lo  = _canvas.canvasToView(selected->pos);
    const glm::vec2 hi  = _canvas.canvasToView(selected->pos + selected->extent);
    const glm::vec2 mid = (lo + hi) * 0.5f;

    constexpr float kHalf = 5.0f; // 10x10 px grab box, independent of zoom
    const auto onHandle = [&](float x, float y) {
        return std::fabs(viewPoint.x - x) <= kHalf && std::fabs(viewPoint.y - y) <= kHalf;
    };

    uint8_t mask = 0;
    if (onHandle(lo.x, lo.y)) mask |= kResizeHandleLeft | kResizeHandleTop;
    if (onHandle(hi.x, lo.y)) mask |= kResizeHandleRight | kResizeHandleTop;
    if (onHandle(lo.x, hi.y)) mask |= kResizeHandleLeft | kResizeHandleBottom;
    if (onHandle(hi.x, hi.y)) mask |= kResizeHandleRight | kResizeHandleBottom;
    if (onHandle(mid.x, lo.y)) mask |= kResizeHandleTop;
    if (onHandle(mid.x, hi.y)) mask |= kResizeHandleBottom;
    if (onHandle(lo.x, mid.y)) mask |= kResizeHandleLeft;
    if (onHandle(hi.x, mid.y)) mask |= kResizeHandleRight;
    return mask;
}

} // namespace ya
