#pragma once

// ============================================================================
// EditorUIDesignerSession - Game UI document/preview model, not a retained chrome tab.
//
// Edits one UIDocument through a live PREVIEW WidgetTree that is strictly
// separate from the runtime tree: PIE mounts fresh instances from scene
// entries, so preview and PIE state never pollute each other.
// Retained EditorSurface tab owns chrome controls; preview/canvas manipulation
// stays on this data layer.
//
// The preview runs in AUTHORING mode, and that is a contract rather than a
// coincidence: the tree lays out, paints and answers geometric picking, and it
// does neither of the two things that would make it a second runtime --
// `tick` (animation, self-refreshing widgets) and `dispatchEvent` (hover, focus,
// capture, click handlers). The tree is private and no accessor exposes it, so a
// canvas click can select and drag a widget but can never reach a UIButton's
// onClick: that belongs to GameUIHost's runtime tree, which is a different
// instance of the same document. Interactive Preview, if it is ever wanted,
// should be an explicit mode with its own clock rather than the default here.
//
// The preview is shown by the UI Designer's Canvas tab (EditorUICanvasTab),
// never by the Level viewport. The tab owns the pointer gestures; this session
// owns the edited widget, the drag session and the canvas view state.
//
// This session is the only writer of the open document. Every edit, whichever
// tool made it, ends in `commitEdit`: the document JSON before and after goes
// onto the document's undo stack, the document is marked dirty and published
// to the store. Undo rebuilds the preview from a snapshot, so every preview
// widget pointer is invalidated by it; holders compare `previewGeneration()`.
// ============================================================================

#include "GUI/Binding/UndoStack.h"
#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorUICanvasView.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <set>
#include <vector>

namespace ya
{

enum class ECanvasAnchorPreset : uint8_t;
class EditorUISlotEdit;

struct EditorDocumentRegistry;
struct EditorLayer;
struct SceneWidgetEntry;
struct UIDocumentStore;

struct EditorUIDesignerSession
{
    explicit EditorUIDesignerSession(EditorLayer* owner);
    ~EditorUIDesignerSession();

    EditorUIDesignerSession(const EditorUIDesignerSession&)            = delete;
    EditorUIDesignerSession& operator=(const EditorUIDesignerSession&) = delete;

    // === Document lifecycle ===
    [[nodiscard]] bool hasDocument() const { return _document != nullptr; }

    /// Open the Game UI document asset at `path` through the store. False when
    /// the store is unbound or the document fails to instantiate.
    bool openDocument(std::string_view path);
    /// Create a fresh untitled document with the given root type. It has no
    /// path yet, so saving only refreshes the in-memory document.
    void newDocument(const std::string& typeId);
    /// Open the document a scene entry mounts. The entry only carries the
    /// reference, so the edited instance is the store's.
    bool openSceneEntry(const SceneWidgetEntry& entry);
    /// Rebuild the document from the preview and persist it. A document with a
    /// path is published to the store (so the hierarchy and the mounted tree
    /// see the edit immediately) and written to disk; an untitled one is only
    /// held. Returns false (with diagnostics) when nothing is open or the
    /// document is invalid. Clears document dirty on success.
    bool saveDocument();
    /// The currently open document. For an asset it is the store's instance,
    /// so the inspector and the mounted tree read the same edits.
    [[nodiscard]] const std::shared_ptr<UIDocument>& getOpenDocument() const { return _document; }
    /// Empty when the open document is untitled.
    [[nodiscard]] const std::string& getDocumentPath() const { return _documentPath; }
    [[nodiscard]] UIElement* getPreviewRoot() const { return _previewRoot.get(); }
    [[nodiscard]] EditorDocumentSession* documentSession() { return _session; }
    [[nodiscard]] const EditorDocumentSession* documentSession() const { return _session; }
    [[nodiscard]] bool isDocumentDirty() const { return _session && _session->dirty(); }
    /// Close honoring RejectIfDirty. Returns false when dirty; the document stays.
    bool closeDocument();
    /// Close the current document and drop the preview (discard dirty).
    void clearDocument();
    /// Detach-time drop: ignore dirty, still honor Locked.
    void abandonDocument();

    // === Edit transactions ===
    /// Record the preview's current state as one undoable edit: snapshot the
    /// document, push before/after onto the undo stack, mark dirty and publish
    /// to the store (memory only). No-op when nothing changed. Commits with
    /// the same non-empty `mergeKey` inside one UndoStack merge session become
    /// one step.
    void commitEdit(std::string label, std::string mergeKey = {});
    /// The open document's undo history (a local stack when no registry).
    [[nodiscard]] UndoStack& undoStack();
    /// Bumped whenever the preview tree is rebuilt (open, undo, redo, close).
    [[nodiscard]] uint64_t previewGeneration() const { return _previewGeneration; }
    /// Child-index path of `widget` from the preview root (`{}` is the root);
    /// nullopt when it is not in the preview.
    [[nodiscard]] std::optional<std::vector<size_t>> childPathOf(const UIElement* widget) const;

    // === Designer display toggles (the UI Tree's eye) ===
    /// Whether the canvas preview skips `path`'s subtree. Authoring-display
    /// state only: runtime visibility is the document's own property and is
    /// not read or written here. The set is cleared whenever the preview tree
    /// is rebuilt or structurally edited, because child paths shift with the
    /// siblings around them.
    [[nodiscard]] bool isDesignerHidden(const std::vector<size_t>& path) const
    {
        return _designerHidden.contains(path);
    }
    void toggleDesignerHidden(const std::vector<size_t>& path);

    // === Canvas view (the Canvas tab's navigation and picture) ===
    [[nodiscard]] EditorUICanvasView&       canvas() { return _canvas; }
    [[nodiscard]] const EditorUICanvasView& canvas() const { return _canvas; }
    /// Preview tree size: the resolution the game lays this UI out at. A
    /// designer setting, not document data -- the same document runs at any
    /// window size. Changing it re-lays the preview and refits the canvas.
    void setDesignResolution(glm::uvec2 size);
    [[nodiscard]] glm::uvec2 designResolution() const { return _designResolution; }

    // === Preview (independent WidgetTree, never shared with the runtime) ===
    /// Build the immutable preview frame. `uiScale`/`offset` map tree-local
    /// logical pixels to render-target pixels (the canvas passes its
    /// framebuffer scale * zoom and pan so the preview stays coherent with
    /// the canvas grid and with canvas picking).
    [[nodiscard]] UIFrameSnapshot buildPreviewSnapshot(const glm::vec2& uiScale, const glm::vec2& offset);
    /// Topmost widget under a canvas-logical point (for editor picking).
    [[nodiscard]] UIElement* pickAt(const glm::vec2& logicalPoint);
    /// Selection is not a document edit, but it is the selection an undo of
    /// the next edit returns to.
    void select(UIElement* widget);
    void clearSelection() { select(nullptr); }
    /// Currently selected preview widget (nullptr when none / not attached).
    [[nodiscard]] UIElement* getSelectedWidget() const
    {
        return (_selected && _selected->isAttached()) ? _selected : nullptr;
    }
    /// Layout rect of the selection in tree-local logical pixels, or nullptr
    /// when nothing is selected. Used by the canvas overlay (selection
    /// outline + resize handles) and by direct manipulation hit tests.
    [[nodiscard]] const Rect2D* getSelectedLayoutRect() const;
    /// Select a widget inside the preview tree by child-index path from the
    /// root (the Scene Hierarchy's entry tree uses this to jump into the
    /// designer document).
    void selectByChildPath(const std::vector<size_t>& path);
    /// Resolve a child-index path from the preview root (`{}` is the root).
    [[nodiscard]] UIElement* findByChildPath(const std::vector<size_t>& path) const;
    /// Mark the preview layout dirty (called after direct edits so the next
    /// snapshot reflects them).
    void invalidatePreview();
    /// Delete a preview widget from the tree. The document root cannot be
    /// deleted (a UIDocument always has exactly one root). Returns true when
    /// the widget was removed.
    bool deleteWidget(UIElement* widget);
    /// The document slot of a preview widget. nullptr for the document root:
    /// its parent edge belongs to the designer host, not to the document.
    [[nodiscard]] std::unique_ptr<EditorUISlotEdit> editSlot(UIElement* widget) const;
    /// Re-anchor a canvas child to a preset, keeping its current size; one
    /// edit. False when the widget is not a canvas child.
    bool applyCanvasAnchorPreset(UIElement* widget, ECanvasAnchorPreset preset);
    // === Snapping (designer preference, not document data) ===
    /// Move/resize snap to the design-pixel grid while enabled; nudge always
    /// moves in exact pixels (fine adjustment never quantizes).
    [[nodiscard]] bool  isSnapToGrid() const { return _bSnapToGrid; }
    void                setSnapToGrid(bool enabled);
    [[nodiscard]] float snapGridSize() const { return kSnapGridSize; }
    /// Arrow-key nudge: move the selection's canvas slot offset by `delta`
    /// design px and commit one "Nudge" edit under merge key "nudge" — the
    /// caller opens/closes the undo merge session around a held-arrow burst.
    /// False when the selection is not a canvas child.
    bool nudgeSelection(const glm::vec2& canvasDelta);

    // === Canvas direct manipulation (the Canvas tab drives the pointer, this
    // session owns the edited widget and the drag session) ===
    /// Resize-handle edge bits.
    static constexpr uint8_t kResizeHandleLeft   = 1u << 0;
    static constexpr uint8_t kResizeHandleRight  = 1u << 1;
    static constexpr uint8_t kResizeHandleTop    = 1u << 2;
    static constexpr uint8_t kResizeHandleBottom = 1u << 3;
    /// Resize handles of the selection under a canvas-view point (0 when none).
    /// Handles are fixed-size squares in view pixels, matching the selection
    /// overlay the canvas compose draws.
    [[nodiscard]] uint8_t hitTestResizeHandles(const glm::vec2& viewPoint) const;
    /// Begin a move session; snapshots position/size/anchors so deltas are
    /// always relative to the press point.
    void beginMove(UIElement* widget, const glm::vec2& canvasPoint);
    /// Begin a resize session with the given edge/corner mask.
    void beginResize(UIElement* widget, const glm::vec2& canvasPoint, uint8_t resizeMask);
    /// Apply a canvas-logical-pixel delta (move or resize per the session
    /// mode). Returns false when the session is invalid (widget detached).
    bool applyDragDelta(const glm::vec2& canvasDelta);
    /// End the session; a drag that moved commits one edit.
    void endDrag();
    /// Whether a move/resize session is active on the given widget.
    [[nodiscard]] bool isDragging(UIElement* widget) const { return _dragWidget == widget && _dragMode != EDragMode::None; }

    // === Designer widget-tree drag-drop (UMG-style hierarchy editing) ===
    enum class EDropPos : uint8_t
    {
        Before,
        Into,
        After,
    };
    /// Drop position from the mouse Y within a row (before / into / after).
    static EDropPos computeDropPos(float itemMinY, float itemMaxY, float mouseY);
    /// Apply a designer-tree drag-drop (reparent/reorder in the preview).
    void applyWidgetDrop(UIElement* dragged, UIElement& target, EDropPos position);
    /// Add a widget from the palette into the selected host (or the document
    /// root). A selected leaf (no layout) cannot host children, so the new
    /// widget goes right after it instead.
    [[nodiscard]] bool addPaletteWidget(const std::string& typeId);
    /// Copy `widget`'s subtree and parent slot in right after it and select the
    /// copy; one edit. The document root cannot be duplicated.
    UIElement* duplicateWidget(UIElement* widget);
    /// Display name for palette entries (`engine.button` → `button`).
    [[nodiscard]] static std::string paletteDisplayName(const std::string& typeId);

  private:
    void rebuildDocumentFromPreview();
    void markDirty();
    void publishDocument();
    /// Drop the drag session without committing (the preview is being replaced).
    void cancelDrag();
    void restoreSnapshot(const nlohmann::json& snapshot, const std::optional<std::vector<size_t>>& selection);
    /// Install an in-memory document under a fresh untitled session key.
    void openUntitled(const std::shared_ptr<UIDocument>& document);
    [[nodiscard]] EditorDocumentRegistry* documents() const;
    [[nodiscard]] UIDocumentStore*        documentStore() const;
    bool adoptSession(const FEditorDocumentId& id);
    bool closeSession(EEditorDocumentCloseMode mode);
    void dropLocalDocument();
    bool installPreview(const std::shared_ptr<UIDocument>& document);

    EditorLayer* _owner = nullptr;
    EditorDocumentSession*  _session   = nullptr;

    std::shared_ptr<UIDocument> _document;
    std::unique_ptr<WidgetTree> _previewTree;
    UIElementRef                _previewRoot;
    UIElement*                  _selected = nullptr;

    /// Asset path of the open document; empty for an untitled one.
    std::string _documentPath;

    EditorUICanvasView _canvas;
    glm::uvec2         _designResolution = {1280, 720};

    // === Edit transaction state ===
    /// The preview as of the last commit: the "before" of the next edit.
    nlohmann::json                     _committedJson;
    std::optional<std::vector<size_t>> _committedSelection;
    /// Designer-display-hidden subtrees (the tree's eye), keyed by child path.
    std::set<std::vector<size_t>> _designerHidden;
    UndoStack                          _localUndo;
    uint64_t                           _previewGeneration = 0;
    /// Undo closures outlive this session on a shared document stack; they
    /// hold this weakly and do nothing once it is gone.
    std::shared_ptr<EditorUIDesignerSession*> _lifetime = std::make_shared<EditorUIDesignerSession*>(this);

    // === Canvas direct-manipulation session state ===
    enum class EDragMode : uint8_t
    {
        None,
        Move,
        Resize,
    };

    static constexpr float kSnapGridSize = 8.0f;
    /// Preview opacity for widgets the runtime would not render (Hidden
    /// subtree). The canvas must show the document being edited, so excluded
    /// widgets read as dimmed ghosts; Collapsed has no rect and stays blank.
    static constexpr float kDesignerGhostOpacity = 0.35f;
    /// Round an offset axis onto the grid (whole multiples), when enabled.
    [[nodiscard]] float snapAxis(float value) const
    {
        return _bSnapToGrid ? std::round(value / kSnapGridSize) * kSnapGridSize : value;
    }
    [[nodiscard]] glm::vec2 snapOffset(glm::vec2 offset) const
    {
        return {snapAxis(offset.x), snapAxis(offset.y)};
    }

    bool _bSnapToGrid = true;

    EDragMode _dragMode        = EDragMode::None;
    UIElement* _dragWidget     = nullptr;
    uint8_t   _resizeMask      = 0;
    glm::vec2 _dragStartPos    = {};
    glm::vec2 _dragStartSize   = {};
    glm::vec2 _dragStartAnchorMin = {};
    glm::vec2 _dragStartAnchorMax = {};
    glm::vec2 _dragStartParentExtent = {};
    bool      _bDragMoved      = false;
};

} // namespace ya
