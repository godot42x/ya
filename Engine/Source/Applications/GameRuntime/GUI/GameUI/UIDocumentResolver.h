#pragma once

// ============================================================================
// UIDocumentResolver - the single `.yaui` resolve entry shared by Editor,
// PIE and Runtime (ui-widget-tree-refactor review Phase 8).
//
// One class owns the path -> UIDocument cache and the load rules; Editor
// (UI Designer) and Runtime (DefaultGameUIController) use the SAME rules
// (VirtualFileSystem read + UIDocument::fromJson), so schema/version/typeId
// behavior can never diverge between preview and runtime. Instantiating a
// cached document always produces an independent detached subtree.
// ============================================================================

#include "GUI/Widgets/UIDocument.h"

#include <string>
#include <unordered_map>

namespace ya
{

struct YA_GAME_RUNTIME_API UIDocumentResolver
{
    /// Load + cache a `.yaui` document. Returns nullptr with a diagnostic
    /// (path + error) when the file is missing or the document is invalid.
    [[nodiscard]] std::shared_ptr<UIDocument> load(const std::string& path);

    /// Drop one entry (or everything) so the next load re-reads the file.
    void invalidate(const std::string& path) { _cache.erase(path); }
    void clearCache() { _cache.clear(); }

    /// Whether a previous load succeeded (resolved status for the editor).
    [[nodiscard]] bool isResolved(const std::string& path) const { return _cache.contains(path); }

  private:
    std::unordered_map<std::string, std::shared_ptr<UIDocument>> _cache;
};

} // namespace ya
