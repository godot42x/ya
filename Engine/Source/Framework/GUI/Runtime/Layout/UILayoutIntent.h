#pragma once

// ============================================================================
// UILayoutIntent - the unified parent->child layout contract.
//
// The historical model let a CHILD author its own stretch geometry
// (TUIWidgetBuilder::fillParent/setAnchors writing child anchors), which
// a path-A parent silently dropped. The unified model moves ALL layout intent
// onto the parent->child edge:
//
//   - the parent decides the layout algorithm (path-A: it assigns every child
//     rect from the slot), and
//   - the slot carries the parameters (anchor Min/Max, offset, min/max size).
//
// A child therefore never carries its own anchor geometry. UILayoutIntent is
// the merged "which layout + its slot" value; concrete widget slots
// (FBoxSlotArgs, FCanvasSlotArgs, ...) are the per-layout parameter shapes.
// ============================================================================

#include "GUI/Layout/UILayout.h"

#include <glm/glm.hpp>
#include <limits>

namespace ya
{

// EWidgetSizeMode is declared in UILayout.h (included above) so that UILayout
// itself can use it for per-axis size resolution.

/// Available space a parent offers a child during measure (path-A contract).
/// `min`/`max` are in logical pixels; `max` defaults to +inf so a child may
/// grow to fill.
struct UIConstraints
{
    glm::vec2 min = {0.0f, 0.0f};
    glm::vec2 max = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
};

/// Unified parent->child layout intent: the layout algorithm the parent applies
/// plus the slot parameters for that layout. `canvas` is the path-B (anchor)
/// parameter block; a path-A host ignores it and uses its own typed slot.
/// Children never author any of this - it lives only on the parent->child edge.
struct UILayoutIntent
{
    EWidgetSizeMode    sizeMode = EWidgetSizeMode::Fixed;
    FCanvasSlotArgs canvas{};
};

} // namespace ya
