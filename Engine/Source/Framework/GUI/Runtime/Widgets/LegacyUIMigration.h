#pragma once

// ============================================================================
// LegacyUIMigration - old Node2D-era Scene UI data -> SceneWidgetEntry /
// UIDocument (ui-widget-tree-refactor Phase 2b).
//
// Pure JSON-to-JSON migration: the legacy scene format stores Game UI as
// `nodeType` + reflected `fields` subtrees in the scene node tree. This
// translator maps those into `.yaui` UIDocuments:
//   - legacy type name -> registry typeId (UICanvasNode is structural: its
//     children become separate top-level entries);
//   - `__base__.Node2D` block -> `__base__.UIElement` (enum value names are
//     identical between EUI* and EWidget*, so field values carry over);
//   - `_visible` (bool, pre-`_visibility` schema) -> `_visibility` enum.
// Unknown legacy types report a diagnostic and produce no document (never a
// silent empty node).
// ============================================================================

#include "GUI/Widgets/UIDocument.h"

#include <string>
#include <vector>

namespace ya
{

/// One migrated document plus the legacy authoring metadata needed by the
/// Scene entry (name for the entryId, zOrder for entry ordering).
struct LegacyUIMigrationResult
{
    std::shared_ptr<UIDocument> document;
    std::string                 name;
    int32_t                     zOrder = 0;
};

/// Migrate one legacy UI node JSON object into UIDocuments. A plain widget
/// yields one document (children recursed); a UICanvasNode yields the
/// documents of its children (the canvas itself is the content layer in the
/// new model). Returns an empty vector (with diagnostics) for unknown types.
[[nodiscard]] std::vector<LegacyUIMigrationResult> migrateLegacyUINode(const nlohmann::json& nodeJson);

/// Map a legacy UI node type name to a registry typeId (empty when unknown).
[[nodiscard]] std::string legacyUITypeToTypeId(const std::string& legacyTypeName);

} // namespace ya
