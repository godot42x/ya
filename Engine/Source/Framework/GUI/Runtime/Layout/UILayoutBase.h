#pragma once

#include "Core/Api.h"
#include "Core/Common/Types.h"
#include "GUI/Layout/UISlot.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace ya
{

struct UIElement;

/// Parent-owned layout algorithm. Layout owns measure/arrange only; visual
/// ownership remains UIElement/WidgetTree and child intent lives in UISlot.
class YA_GUI_API UILayout
{
public:
    virtual ~UILayout() = default;

    void setOwner(UIElement& owner) { _owner = &owner; }
    [[nodiscard]] virtual std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const;
    [[nodiscard]] virtual glm::vec2 measure(const UIElement& parent) const = 0;
    /// Counted public entry. Hosts that override `layoutAssigned` currently
    /// call this without `tryReuseAssignedLayout()`; GAH-002 records that
    /// bypass, GAH-201 must keep the counter when skip is unified.
    void arrange(UIElement& parent, const Rect2D& rect) const;
    [[nodiscard]] uint32_t getArrangeCount() const { return _arrangeCount; }
    void resetArrangeCount() const { _arrangeCount = 0; }

protected:
    virtual void onArrange(UIElement& parent, const Rect2D& rect) const = 0;
    void invalidateMeasure() const;
    void invalidateArrange() const;
    /// Invalidate the owner's whole subtree paint context (clip/visibility),
    /// without re-running measure/arrange.
    void invalidateSubtreePaint() const;

    /// The single entry point for path-A child rect assignment.
    ///
    /// Every onArrange() must route child rects through here instead of calling
    /// child.layoutAssigned() directly: this is what makes the "path-A parents
    /// ignore child anchors" contract observable, so an author who wrote
    /// setAnchors()/fillWidth() on a child of a box/scroll/split/overlay gets a
    /// diagnostic instead of a silently dropped intent.
    void assignChildRect(UIElement& child, const Rect2D& rect) const;

private:
    UIElement* _owner = nullptr;
    mutable uint32_t _arrangeCount = 0;
};

} // namespace ya
