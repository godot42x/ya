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
// ============================================================================

#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"

#include <memory>
#include <string>
#include <vector>

namespace ya
{

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

    /// Open the Game UI document asset at `path` through the store.
    void openDocument(std::string_view path);
    /// Create a fresh untitled document with the given root type. It has no
    /// path yet, so saving only refreshes the in-memory document.
    void newDocument(const std::string& typeId);
    /// Open the document a scene entry mounts. The entry only carries the
    /// reference, so the edited instance is the store's.
    void openSceneEntry(const SceneWidgetEntry& entry);
    /// Rebuild the document from the preview and persist it. A document with a
    /// path is published to the store (so the hierarchy and the mounted tree
    /// see the edit immediately) and written to disk; an untitled one is only
    /// held. Returns false (with diagnostics) when nothing is open or the
    /// document is invalid. Clears document dirty on success.
    bool saveDocument();
    /// The currently open document. For an asset it is the store's instance,
    /// so the inspector and the mounted tree read the same edits.
    [[nodiscard]] const std::shared_ptr<UIDocument>& getOpenDocument() const { return _document; }
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
    /// Rebuild the document from the preview after a structural edit and
    /// publish it to the store (memory only), so the Scene Hierarchy and the
    /// inspector see the edit before an explicit save.
    void syncPreviewToDocument();

    // === Preview (independent WidgetTree, never shared with the runtime) ===
    /// Build the immutable preview frame. `uiScale`/`offset` map tree-local
    /// logical pixels to render-target pixels (the 2D canvas passes its
    /// framebuffer scale * zoom and pan so the preview stays coherent with
    /// the canvas grid and with canvas picking).
    [[nodiscard]] UIFrameSnapshot buildPreviewSnapshot(const glm::vec2& uiScale, const glm::vec2& offset);
    /// Topmost widget under a canvas-logical point (for editor picking).
    [[nodiscard]] UIElement* pickAt(const glm::vec2& logicalPoint);
    void select(UIElement* widget) { _selected = widget; }
    void clearSelection() { _selected = nullptr; }
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

    // === Canvas direct manipulation (EditorLayer drives the mouse, this
    // panel owns the edited widget and the drag session) ===
    /// Resize-handle edge bits (shared with EditorLayer's handle hit test).
    static constexpr uint8_t kResizeHandleLeft   = 1u << 0;
    static constexpr uint8_t kResizeHandleRight  = 1u << 1;
    static constexpr uint8_t kResizeHandleTop    = 1u << 2;
    static constexpr uint8_t kResizeHandleBottom = 1u << 3;
    /// Begin a move session; snapshots position/size/anchors so deltas are
    /// always relative to the press point.
    void beginMove(UIElement* widget, const glm::vec2& canvasPoint);
    /// Begin a resize session with the given edge/corner mask.
    void beginResize(UIElement* widget, const glm::vec2& canvasPoint, uint8_t resizeMask);
    /// Apply a canvas-logical-pixel delta (move or resize per the session
    /// mode). Returns false when the session is invalid (widget detached).
    bool applyDragDelta(const glm::vec2& canvasDelta);
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
    /// Add a widget from the palette under the current selection (or document root).
    [[nodiscard]] bool addPaletteWidget(const std::string& typeId);
    /// Display name for palette entries (`engine.button` → `button`).
    [[nodiscard]] static std::string paletteDisplayName(const std::string& typeId);

  private:
    void rebuildDocumentFromPreview();
    void applyPreviewExtent();
    void markDirty();
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

    // === Canvas direct-manipulation session state ===
    enum class EDragMode : uint8_t
    {
        None,
        Move,
        Resize,
    };

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
