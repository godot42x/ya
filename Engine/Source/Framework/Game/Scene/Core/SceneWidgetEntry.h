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
struct UIInstanceOverrideSet
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
struct SceneWidgetEntry
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

} // namespace ya
