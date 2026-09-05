#pragma once

// ============================================================================
// UIDesignerPanel - Game UI document/preview model, not a retained chrome tab.
//
// Edits one UIDocument through a live PREVIEW WidgetTree that is strictly
// separate from the runtime tree: PIE mounts fresh instances from scene
// entries, so preview and PIE state never pollute each other.
// Retained EditorSurface tab owns chrome controls; preview/canvas manipulation
// stays on this data layer.
// ============================================================================

#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct EditorLayer;
struct Scene;
struct SceneWidgetEntry;

struct UIDesignerPanel
{
    explicit UIDesignerPanel(EditorLayer* owner);
    ~UIDesignerPanel();

    UIDesignerPanel(const UIDesignerPanel&)            = delete;
    UIDesignerPanel& operator=(const UIDesignerPanel&) = delete;

    // === Document lifecycle ===
    [[nodiscard]] bool hasDocument() const { return _document != nullptr; }

    /// Open a document standalone (from a palette type or an externally built
    /// document).
    void openDocument(const std::shared_ptr<UIDocument>& document);
    /// Create a fresh document with the given root type.
    void newDocument(const std::string& typeId);
    /// Open the inline document of a scene entry; saving writes back to the
    /// entry instead of a file.
    void openSceneEntry(Scene& scene, SceneWidgetEntry& entry);
    /// Rebuild + persist the document. In scene-entry mode the inline document
    /// is written back to the entry; otherwise it is held for the next
    /// save/open. Returns false (with diagnostics) when nothing is open or the
    /// document is invalid.
    bool saveDocument();
    /// The currently open document (shared with the scene entry it came from
    /// when opened via openSceneEntry). Used to detect stale designer state
    /// after external document edits (e.g. hierarchy drag-drop).
    [[nodiscard]] const std::shared_ptr<UIDocument>& getOpenDocument() const { return _document; }
    [[nodiscard]] UIElement* getPreviewRoot() const { return _previewRoot.get(); }
    /// Close the current document and drop the preview (no save).
    void clearDocument();
    /// Rebuild the document from the preview after a structural edit and
    /// propagate it (scene-entry mode writes back to the entry's inline
    /// document). Keeps the left hierarchy in sync.
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

    EditorLayer* _owner = nullptr;

    std::shared_ptr<UIDocument> _document;
    std::unique_ptr<WidgetTree> _previewTree;
    UIElementRef                _previewRoot;
    UIElement*                  _selected = nullptr;

    /// Scene-entry edit mode (save writes back to the entry).
    Scene*    _entryScene = nullptr;
    std::string _entryId;

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
