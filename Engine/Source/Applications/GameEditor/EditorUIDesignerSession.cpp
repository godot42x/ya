#include "GameEditor/EditorUIDesignerSession.h"

#include "Core/Log.h"

#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/UIDocumentStore.h"
#include "GUI/Widgets/UITypeRegistry.h"

#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include "Scene/Core/SceneWidgetEntry.h"

#include "GameRuntime/App.h"

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
    _document.reset();
    _previewTree.reset();
    _previewRoot.reset();
    _selected = nullptr;
    _documentPath.clear();
    endDrag();
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
    _documentPath = std::string(path);
    if (_owner) {
        _owner->setViewportMode(EViewportMode::Mode2D);
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
    _documentPath.clear();
    if (_owner) {
        _owner->setViewportMode(EViewportMode::Mode2D);
    }
}

bool EditorUIDesignerSession::installPreview(const std::shared_ptr<UIDocument>& document)
{
    _document     = document;
    _previewTree  = std::make_unique<WidgetTree>(Extent2D{800, 600});
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
    return true;
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

void EditorUIDesignerSession::selectByChildPath(const std::vector<size_t>& path)
{
    _selected = findByChildPath(path);
}

void EditorUIDesignerSession::syncPreviewToDocument()
{
    if (!_previewRoot) {
        return;
    }
    auto synced = UIDocument::fromWidget(*_previewRoot);
    if (!synced) {
        return;
    }
    _document = std::move(synced);
    markDirty();

    // Publish the edit so the Scene Hierarchy / inspector read the same
    // document. The file is only written on an explicit save.
    if (!_documentPath.empty()) {
        if (UIDocumentStore* store = documentStore()) {
            store->put(_documentPath, _document);
        }
    }
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
    // The document must follow the preview, and the hierarchy must follow
    // the document (UMG-style live editing).
    syncPreviewToDocument();
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

    UIElement* parent = (_selected && _selected->isAttached()) ? _selected : _previewRoot.get();
    if (!parent) {
        return false;
    }
    _previewTree->attach(*parent, widget);
    _selected = widget.get();
    syncPreviewToDocument();
    return true;
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
                slot->setOffset(_dragStartPos + canvasDelta);
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

void EditorUIDesignerSession::endDrag()
{
    const bool moved = _bDragMoved;
    _dragMode   = EDragMode::None;
    _dragWidget = nullptr;
    _resizeMask = 0;
    _bDragMoved = false;
    if (moved) {
        markDirty();
    }
}

void EditorUIDesignerSession::applyPreviewExtent()
{
    if (!_previewTree || !_owner) {
        return;
    }
    const glm::vec2 viewportSize = _owner->getViewportSize();
    if (viewportSize.x > 1.0f && viewportSize.y > 1.0f) {
        _previewTree->setLogicalExtent(Extent2D::fromVec2(viewportSize));
    }
}

} // namespace ya
