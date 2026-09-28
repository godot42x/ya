#pragma once

// ============================================================================
// Authored behaviour descriptions on widget nodes.
//
// A document node may carry `behaviors: [{ "type": ..., "data": {...} }]`.
// The GUI stores and round-trips them but never interprets them: `type` and
// `data` are opaque. Something outside the GUI (the runtime Game UI host)
// turns them into live UIBehaviors through IUIBehaviorActivator. A tree that
// is given no activator (editor preview, designer) keeps the descriptions and
// runs nothing.
// ============================================================================

#include "Core/Common/Types.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace ya
{

struct UIElement;

struct FUIBehaviorSpec
{
    std::string    type;
    nlohmann::json data = nlohmann::json::object();

    bool operator==(const FUIBehaviorSpec&) const = default;
};

/// What an activation belongs to: the mounted entry and its root widget.
struct FUIBehaviorActivation
{
    std::string_view entryId;
    UIElement&       entryRoot;
};

struct IUIBehaviorActivator
{
    virtual ~IUIBehaviorActivator() = default;

    /// Turn one spec on `widget` into live state. Reporting an unknown type is
    /// the activator's call; the GUI has no list of valid types.
    virtual void activate(UIElement& widget, const FUIBehaviorSpec& spec, const FUIBehaviorActivation& context) = 0;
};

/// Call `activator` once per spec, visiting `root`'s subtree in preorder and a
/// node's specs in authored order.
YA_GUI_API void activateBehaviorSpecs(UIElement& root, IUIBehaviorActivator& activator, const FUIBehaviorActivation& context);

} // namespace ya
