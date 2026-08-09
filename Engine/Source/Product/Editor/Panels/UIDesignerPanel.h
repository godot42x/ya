#pragma once

// ============================================================================
// UIDesignerPanel - Game UI authoring (ui-widget-tree-refactor Phase 5).
//
// Edits one UIDocument (`.yaui`) through a live PREVIEW WidgetTree that is
// strictly separate from the runtime tree: PIE mounts fresh instances from
// the scene entries, so preview and PIE state never pollute each other.
//   - palette:      registered widget types (UITypeRegistry, stable IDs)
//   - tree:         the preview tree's widget hierarchy
//   - inspector:    reflected fields of the selected preview widget
//   - save:         rebuilds the document from the preview (fromWidget)
// The editor shell stays fully ImGui; the preview can also be composited
// into the 2D canvas via buildPreviewSnapshot().
// ============================================================================

#include "Editor/FilePicker.h"

#include "Host/GUI/GameUI/UIDocumentResolver.h"

#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <memory>
#include <string>

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

    void onImGuiRender();

    // === Document lifecycle ===
    [[nodiscard]] bool hasDocument() const { return _document != nullptr; }
    [[nodiscard]] const std::string& getDocumentPath() const { return _documentPath; }

    /// Open a document standalone (from a `.yaui` file or a new palette type).
    void openDocument(const std::shared_ptr<UIDocument>& document, const std::string& path);
    /// Open a `.yaui` file from disk (parse + instantiate preview).
    void openDocumentPath(const std::string& path);
    /// Create a fresh document with the given root type.
    void newDocument(const std::string& typeId);
    /// Open the inline document of a scene entry; saving writes back to the
    /// entry instead of a file.
    void openSceneEntry(Scene& scene, SceneWidgetEntry& entry);
    /// Rebuild + persist the document (file or scene entry). Returns false
    /// (with diagnostics) when nothing is open or the document is invalid.
    bool saveDocument();

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

  private:
    void drawToolbar();
    void drawWidgetTree(UIElement& widget);
    void drawPalette();
    void drawInspector();
    void rebuildDocumentFromPreview();
    void applyPreviewExtent();

    EditorLayer* _owner = nullptr;

    std::shared_ptr<UIDocument> _document;
    std::string                 _documentPath;
    std::unique_ptr<WidgetTree> _previewTree;
    UIElementRef                _previewRoot;
    UIElement*                  _selected = nullptr;

    /// Scene-entry edit mode (save writes back to the entry).
    Scene*    _entryScene = nullptr;
    std::string _entryId;

    /// Preview resolver: same rules as the runtime host resolver.
    UIDocumentResolver _documentResolver;

    char    _savePathBuffer[512] = "";
    FilePicker _filePicker;
};

} // namespace ya
