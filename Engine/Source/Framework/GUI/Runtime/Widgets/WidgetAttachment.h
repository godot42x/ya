#pragma once

// ============================================================================
// WidgetAttachment - the handle returned by WidgetTree::attach*.
//
// detach() unmounts the widget from the tree (recursively, for the whole
// subtree) but never destroys it: business-held UIElementRefs keep the
// detached widget alive. Calling detach() on an already-detached widget is a
// no-op, so the handle is safe to keep across scene switches.
// ============================================================================

#include <memory>

namespace ya
{

struct UIElement;
struct WidgetTree;

struct WidgetAttachment
{
    WidgetTree*             tree   = nullptr;
    std::weak_ptr<UIElement> widget;

    /// Whether this handle currently represents a live attachment.
    [[nodiscard]] bool valid() const { return tree != nullptr; }

    /// Detach the referenced widget (no-op if already detached / destroyed).
    void detach();
};

} // namespace ya
