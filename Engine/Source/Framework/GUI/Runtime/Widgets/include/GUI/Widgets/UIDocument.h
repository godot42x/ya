#pragma once

// ============================================================================
// UIDocument - reusable Game UI authoring data (ui-widget-tree-refactor
// Phase 2).
//
//   {
//     "version": 1,
//     "typeId":  "engine.panel",
//     "fields":  { ...reflected field values... },
//     "children": [ ...nested UIDocument... ]
//   }
//
// A document is a detached UIElement subtree template: it does not depend on
// a Scene, a WidgetTree or a World. instantiate() creates an independent
// subtree through UITypeRegistry (stable type ID + module lease); instance
// mutable state is never shared between two instantiations.
//
// This phase stores plain reflected fields (serializable + runtime-visible).
// InstanceEditable override containers arrive with SceneWidgetEntry; no
// structural prefab diff is implemented.
// ============================================================================

#include "GUI/Widgets/UIElement.h"

#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct YA_GUI_API UIDocument
{
    static constexpr uint32_t kFormatVersion = 2;

    /// Stable registry type ID of the root widget (required).
    std::string typeId;
    /// Reflected field values of the root widget (base + own).
    nlohmann::json fields;
    /// Child documents, attached as children of the instantiated root.
    std::vector<std::shared_ptr<UIDocument>> children;
    /// Parent-owned slot intent for each child, kept index-aligned with
    /// `children`.  Slot data belongs to the parent edge, never the child.
    std::vector<nlohmann::json> childSlots;

    /// Capture a detached widget subtree into a document. The widget must
    /// carry a registry type ID (created via UITypeRegistry).
    [[nodiscard]] static std::shared_ptr<UIDocument> fromWidget(const UIElement& widget);

    /// Instantiate an independent detached subtree. Returns nullptr (with a
    /// diagnostic log) when the type ID is unknown or fields fail to apply.
    [[nodiscard]] UIElementRef instantiate() const;

    /// Serialize the document to its JSON form.
    [[nodiscard]] nlohmann::json toJson() const;
    /// Parse a JSON form. Returns nullptr for unknown versions or a malformed
    /// document.
    [[nodiscard]] static std::shared_ptr<UIDocument> fromJson(const nlohmann::json& json);
};

} // namespace ya
