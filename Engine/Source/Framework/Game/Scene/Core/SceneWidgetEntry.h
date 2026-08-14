#pragma once

// ============================================================================
// SceneWidgetEntry - authoring recipe for one top-level Game UI item stored
// in a Scene (ui-widget-tree-refactor Phase 2b).
//
// Pure authoring data: no live widget pointers, no tree/parent state, no
// Scene dependency. A Scene stores a list of entries; the GameUIHost (Phase 3)
// instantiates autoMount entries into the active WidgetTree when the scene
// activates and unmounts them on deactivate.
//
// `documentPath` and `inlineDocument` are mutually exclusive:
//   - documentPath  - reference to a `.yaui` asset (resolution happens in the
//                     resource/host layer, Phase 3)
//   - inlineDocument - inline UIDocument definition
// ============================================================================

#include "GUI/Widgets/UIDocument.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace ya
{

/// Instance-level field overrides for one entry. Only InstanceEditable fields
/// may be overridden; the metadata filter arrives with the editor phase
/// (Phase 5). No structural diff (child add/remove/reorder) is supported.
struct YA_SCENE_CORE_API UIInstanceOverrideSet
{
    /// field name -> reflected field value (JSON form, base + own allowed).
    std::unordered_map<std::string, nlohmann::json> fieldOverrides;

    [[nodiscard]] bool empty() const { return fieldOverrides.empty(); }

    /// Apply the overrides to a widget. Fields that do not exist in the
    /// widget's reflected type are rejected with a diagnostic (never silently
    /// ignored). Returns true when every override applied.
    bool applyTo(UIElement& widget) const;

    [[nodiscard]] nlohmann::json toJson() const;
    static UIInstanceOverrideSet fromJson(const nlohmann::json& json);
};

/// One top-level Game UI authoring entry inside a Scene.
struct YA_SCENE_CORE_API SceneWidgetEntry
{
    /// Stable within a Scene; used by editor selection and overrides.
    std::string entryId;
    /// `.yaui` document reference (mutually exclusive with inlineDocument).
    std::string documentPath;
    /// Inline UIDocument definition (mutually exclusive with documentPath).
    std::shared_ptr<UIDocument> inlineDocument;
    /// Paint/hit order among entries in the content layer.
    int32_t zOrder = 0;
    /// Whether the default controller auto-instantiates on scene activation.
    bool autoMount = true;
    /// InstanceEditable field overrides (empty in the authoring phase).
    UIInstanceOverrideSet overrides;

    [[nodiscard]] nlohmann::json toJson() const;
    static SceneWidgetEntry fromJson(const nlohmann::json& json);
};

/// Drop position for document-level reparenting (Game UI hierarchy
/// drag-drop; the editor's ENodeDropPosition mirrors these values).
enum class EWidgetEntryDropPosition : uint8_t
{
    Before,
    Into,
    After,
};

/// Move a UIDocument subtree between scene widget entries (the Game UI
/// hierarchy drag-drop operation). `srcPath`/`dstPath` are child-index paths
/// inside the entry root document (empty = the entry root itself).
///
///   Into        - the source document becomes a child of the target document
///   Before/After- the source becomes a sibling of the target (when both
///                 paths are empty this reorders the entries themselves)
///
/// A top-level (empty srcPath) source nested into another top-level entry
/// keeps its visual position: the position fields are converted from
/// canvas-relative to parent-relative (both must be point-anchored).
///
/// `resolveFile` lets documentPath entries participate: their documents are
/// loaded on demand (the editor passes the host UIDocumentResolver). File-
/// backed documents mutated by the move are reported in `changedFiles` so the
/// caller can persist them.
///
/// Returns true on success; mutates `entries` in place. Fails (with a
/// diagnostic) on unresolvable paths, cycles, non-inline targets, or when a
/// nested widget would need to become a top-level entry.
YA_SCENE_CORE_API bool moveWidgetEntryDocument(std::vector<SceneWidgetEntry>& entries,
                                               size_t                    srcEntryIndex,
                                               const std::vector<size_t>& srcPath,
                                               size_t                    dstEntryIndex,
                                               const std::vector<size_t>& dstPath,
                                               EWidgetEntryDropPosition  position,
                                               const std::function<std::shared_ptr<UIDocument>(const std::string&)>& resolveFile = {},
                                               std::vector<std::string>* changedFiles = nullptr);

/// Validation-only preview of moveWidgetEntryDocument: runs the same rules
/// (documents resolvable, not a self-drop, no cycle, nested-into-entry-root
/// restriction) WITHOUT mutating `entries`. The editor uses it to reject
/// invalid drops visually (red feedback) before delivery. Unlike the move,
/// self-drops and no-ops report false (there is nothing meaningful to do).
YA_SCENE_CORE_API bool canMoveWidgetEntryDocument(std::vector<SceneWidgetEntry>& entries,
                                                  size_t                    srcEntryIndex,
                                                  const std::vector<size_t>& srcPath,
                                                  size_t                    dstEntryIndex,
                                                  const std::vector<size_t>& dstPath,
                                                  EWidgetEntryDropPosition  position,
                                                  const std::function<std::shared_ptr<UIDocument>(const std::string&)>& resolveFile = {});

} // namespace ya
