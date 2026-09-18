#include "GameEditor/Panels/UIDesignerPanel.h"

#include "Core/Log.h"

#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/UITypeRegistry.h"

#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include "Scene/Core/Scene.h"

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

UIDesignerPanel::UIDesignerPanel(EditorLayer* owner) : _owner(owner)
{
}

UIDesignerPanel::~UIDesignerPanel()
{
    abandonDocument();
}

EditorDocumentRegistry* UIDesignerPanel::documents() const
{
    if (_documents) {
        return _documents;
    }
    return _owner ? _owner->documentRegistry() : nullptr;
}

void UIDesignerPanel::markDirty()
{
    if (_session) {
        _session->markDirty();
    }
}

void UIDesignerPanel::dropLocalDocument()
{
    _document.reset();
    _previewTree.reset();
    _previewRoot.reset();
    _selected   = nullptr;
    _entryScene = nullptr;
    _entryId.clear();
    endDrag();
}

bool UIDesignerPanel::closeSession(EEditorDocumentCloseMode mode)
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

bool UIDesignerPanel::adoptSession(const FEditorDocumentId& id)
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

bool UIDesignerPanel::closeDocument()
{
    return closeSession(EEditorDocumentCloseMode::Request);
}

void UIDesignerPanel::clearDocument()
{
    (void)closeSession(EEditorDocumentCloseMode::Discard);
}

void UIDesignerPanel::abandonDocument()
{
    (void)closeSession(EEditorDocumentCloseMode::Force);
}

void UIDesignerPanel::openDocument(const std::shared_ptr<UIDocument>& document)
{
    if (!document) {
        YA_CORE_WARN("UIDesignerPanel::openDocument: null document");
        return;
    }
    EditorDocumentRegistry* docs = documents();
    const FEditorDocumentId id =
        makeEditorUIDocumentId(docs ? docs->makeUntitledKey() : std::string("local"));
    if (!adoptSession(id)) {
        return;
    }
    _document     = document;
    _entryScene   = nullptr;
    _entryId.clear();
    if (!installPreview(document)) {
        (void)closeSession(EEditorDocumentCloseMode::Force);
    }
}

bool UIDesignerPanel::installPreview(const std::shared_ptr<UIDocument>& document)
{
    _document     = document;
    _previewTree  = std::make_unique<WidgetTree>(Extent2D{800, 600});
    _previewTree->setTextureSource(&gameUITextureSource());
    _previewRoot  = document->instantiate();
    _selected     = nullptr;
    if (!_previewRoot) {
        YA_CORE_ERROR("UIDesignerPanel::openDocument: document '{}' failed to instantiate",
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
    YA_CORE_ASSERT(attachment.valid(), "UIDesignerPanel: failed to attach preview root");
    _selected = _previewRoot.get();
    return true;
}

void UIDesignerPanel::newDocument(const std::string& typeId)
{
    auto document     = std::make_shared<UIDocument>();
    document->typeId  = typeId;
    document->fields  = nlohmann::json::object();
    openDocument(document);
}

void UIDesignerPanel::openSceneEntry(Scene& scene, SceneWidgetEntry& entry)
{
    if (!entry.inlineDocument) {
        YA_CORE_WARN("UIDesignerPanel::openSceneEntry: entry '{}' has no inline document", entry.entryId);
        return;
    }
    const FEditorDocumentId id = makeEditorUIDocumentId(scene.getName() + "#" + entry.entryId);
    if (!adoptSession(id)) {
        return;
    }
    _entryScene = &scene;
    _entryId    = entry.entryId;
    if (!installPreview(entry.inlineDocument)) {
        (void)closeSession(EEditorDocumentCloseMode::Force);
        _entryScene = nullptr;
        _entryId.clear();
    }
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
                if (_session) {
                    _session->clearDirty();
                }
                return true;
            }
        }
        YA_CORE_ERROR("UIDesignerPanel::saveDocument: entry '{}' no longer exists", _entryId);
        return false;
    }

    // Standalone document: the rebuilt document is held in `_document`; callers
    // (e.g. scene-entry open) consume it from getOpenDocument(). No file format.
    YA_CORE_INFO("UIDesignerPanel: rebuilt document '{}'", _document->typeId);
    if (_session) {
        _session->clearDirty();
    }
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

UIElement* UIDesignerPanel::findByChildPath(const std::vector<size_t>& path) const
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

void UIDesignerPanel::selectByChildPath(const std::vector<size_t>& path)
{
    _selected = findByChildPath(path);
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
    markDirty();

    // Inline scene-entry mode: write back to the entry so the Scene
    // Hierarchy's Game UI Entries tree reflects the edit immediately.
    if (_entryScene && !_entryId.empty()) {
        for (auto& entry : _entryScene->getWidgetEntries()) {
            if (entry.entryId == _entryId) {
                entry.inlineDocument = _document;
                break;
            }
        }
    }
}

UIDesignerPanel::EDropPos UIDesignerPanel::computeDropPos(float itemMinY, float itemMaxY, float mouseY)
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

std::string UIDesignerPanel::paletteDisplayName(const std::string& typeId)
{
    return shortTypeName(typeId);
}

bool UIDesignerPanel::addPaletteWidget(const std::string& typeId)
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

void UIDesignerPanel::beginMove(UIElement* widget, const glm::vec2& canvasPoint)
{
    if (!widget || !widget->isAttached()) {
        return;
    }
    _dragMode        = EDragMode::Move;
    _dragWidget      = widget;
    _resizeMask      = 0;
    if (!readCanvasIntent(*widget, _dragStartPos, _dragStartSize, _dragStartAnchorMin, _dragStartAnchorMax)) {
        YA_CORE_WARN("UIDesignerPanel::beginMove: widget '{}' is not attached through a canvas slot",
                     widget->_name);
        _dragMode = EDragMode::None;
        _dragWidget = nullptr;
        return;
    }
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
    if (!readCanvasIntent(*widget, _dragStartPos, _dragStartSize, _dragStartAnchorMin, _dragStartAnchorMax)) {
        YA_CORE_WARN("UIDesignerPanel::beginResize: widget '{}' is not attached through a canvas slot",
                     widget->_name);
        _dragMode = EDragMode::None;
        _dragWidget = nullptr;
        return;
    }
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
        if (UIElement* parent = _dragWidget->getParent()) {
            if (auto* slot = dynamic_cast<UICanvasSlot*>(parent->getSlotForChild(*_dragWidget))) {
                slot->setOffset(_dragStartPos + canvasDelta);
                invalidatePreview();
                return true;
            }
        }
        YA_CORE_ERROR("UIDesignerPanel::applyDragDelta: move lost its parent canvas slot");
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

        YA_CORE_ERROR("UIDesignerPanel::applyDragDelta: canvas drag lost its parent canvas slot");
        endDrag();
        return false;
    }

    invalidatePreview();
    return true;
}

void UIDesignerPanel::endDrag()
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

} // namespace ya
