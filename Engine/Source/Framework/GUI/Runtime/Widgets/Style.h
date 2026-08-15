#pragma once

// ============================================================================
// Style - data-driven widget styling (layout/style separation).
//
// A UIStyleSet holds named styles; each style is a Reactive<FWidgetStyle>, so
// widgets bind a style and a style edit marks every dependent paint-dirty via
// the existing reactive invalidation — "change one style, the whole themed UI
// repaints" without touching per-widget color fields.
//
// Minimal by design: fill/text colors + font size + padding. Richer style
// properties (borders, radii, brushes, 9-patch) are later extensions.
// ============================================================================

#include "Core/Api.h"
#include "GUI/Widgets/Reactive.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace ya
{

/// Common style attributes. Deliberately small; controls read only the
/// attributes they draw (text ignores fill, buttons ignore text, ...).
struct FWidgetStyle
{
    glm::vec4 fillColor = {0.8f, 0.8f, 0.8f, 1.0f};
    glm::vec4 textColor = {1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t  fontSize  = 16;
    glm::vec2 padding   = {0.0f, 0.0f};

    bool operator==(const FWidgetStyle& other) const
    {
        return fillColor == other.fillColor &&
               textColor == other.textColor &&
               fontSize  == other.fontSize &&
               padding   == other.padding;
    }
};

/// Named style collection. Styles are Reactive so widgets can bind them and
/// be notified on edit. Owned by the host (or a singleton); not tied to a
/// specific tree so one set themes many widgets/windows.
class YA_GUI_API UIStyleSet
{
public:
    /// Define (or replace) a named style and return its reactive handle.
    std::shared_ptr<Reactive<FWidgetStyle>> define(std::string name, FWidgetStyle style);

    /// Find a style by name; null when undefined.
    [[nodiscard]] std::shared_ptr<Reactive<FWidgetStyle>> find(const std::string& name) const;

    /// Bind a style to a widget. Implemented in .cpp (needs UIElement).
    void bindTo(std::shared_ptr<Reactive<FWidgetStyle>> style, class UIElement& widget);

private:
    std::unordered_map<std::string, std::shared_ptr<Reactive<FWidgetStyle>>> _styles;
};

} // namespace ya
