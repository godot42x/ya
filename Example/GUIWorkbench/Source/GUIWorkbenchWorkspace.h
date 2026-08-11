#pragma once

// ============================================================================
// FWorkbenchWorkspace - the .yaui document workspace (gui-app-bootstrap
// Phase 3 + document loop).
//
// Ownership rule: the document template, selection (child-index path) and
// command state live here; widgets only hold presentation state and report
// actions. The workspace deliberately does not include WidgetTree headers:
// it is testable without the live tree and is the candidate for later
// extraction once the editor consumes the same contract.
//
// The live preview instance is owned by the presenter (FWorkbenchApp); the
// workspace only rebuilds its document template from that instance
// (rebuildFromPreview) and persists it (save*).
// ============================================================================

#include "GUI/Widgets/UIDocument.h"

#include <string>
#include <vector>

namespace guiworkbench
{

/// One flattened document row (pure data, mirrors the presenter's list).
struct FDocumentRow
{
    std::string path;      // child-index path from the root; "" = root
    std::string name;
    std::string typeId;    // full registry type id (may be empty)
    int         depth = 0;
};

/// .yaui document workspace: document + selection + command state.
struct FWorkbenchWorkspace
{
    std::shared_ptr<ya::UIDocument> document;
    std::string                     documentPath;
    /// Selected child-index path ("" = root, empty string when nothing open
    /// is represented by document == nullptr).
    std::string selectedPath;
    bool        bDirty        = false;
    std::string commandResult = "Ready";

    // === Document commands ===
    /// Create a fresh document with the given root widget type.
    void newDocument(const std::string& typeId = "engine.panel");
    /// Load + parse a `.yaui` file. Returns false (with diagnostics) on
    /// read/parse failure.
    bool openDocument(const std::string& path);
    /// Write the document back to `documentPath`. Returns false when there
    /// is no document / no path.
    bool saveDocument();
    /// Write the document to `path` and remember it. Returns false on
    /// failure.
    bool saveDocumentAs(const std::string& path);
    void closeDocument();
    /// Rebuild the document template from the live preview instance
    /// (called by the presenter on save).
    void rebuildFromPreview(const ya::UIElement& previewRoot);

    // === Selection (path-based; the presenter maps paths to live widgets) ===
    void select(const std::string& path);
    /// Move selection by `delta` rows in the flattened document order
    /// (clamped; no-op without a document).
    void selectRelative(int delta);
    [[nodiscard]] const std::string& getSelectedPath() const { return selectedPath; }
    /// Flatten the document into stable rows (depth-first). The presenter
    /// rebuilds its row widgets from this; selection navigation uses the
    /// same order.
    [[nodiscard]] std::vector<FDocumentRow> flattenRows() const;

    // === Mutations ===
    /// Mark the document dirty (called by the presenter after editing the
    /// live instance; rebuildFromPreview is only needed before save).
    void recordMutation() { bDirty = true; }
};

} // namespace guiworkbench
