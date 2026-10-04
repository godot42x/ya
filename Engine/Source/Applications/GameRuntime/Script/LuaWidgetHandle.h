#pragma once

// Script references to Game UI widgets. A reference is weak: once its widget
// leaves the tree, reads other than `valid` return nil, writes do nothing,
// and the handle warns once per widget. `valid` stays false. A widget from
// `self:spawn` works before it is attached.

#include "Core/Scripting/ScriptValue.h"
#include "GUI/Widgets/UIElement.h"

namespace ya
{

struct GameUIHost;

/// A script reference to `widget`, or an empty ref when `widget` is null.
[[nodiscard]] script::ScriptRef widgetScriptRef(GameUIHost& host, const UIElementRef& widget);

} // namespace ya
