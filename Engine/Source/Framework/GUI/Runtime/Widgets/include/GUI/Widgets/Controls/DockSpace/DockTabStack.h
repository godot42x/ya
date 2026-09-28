#pragma once

#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"

namespace ya
{

struct UIDockSpace;

/// Shared drop target for DockArea, TabStack, and TabWell. Area owns overlay
/// and commit; leaf widgets are the hit targets.
void installDockDropTarget(UIElement& widget);

/// DockArea leaf projection: tab well + active content, stacked in one rect.
/// TabStack drop target (center merge / cardinal split / chooser).
/// Does not own `FDockContext`.
struct YA_GUI_API UIDockTabStack : public UIOverlay
{
    YA_REFLECT_BEGIN(UIDockTabStack, UIOverlay)
    YA_REFLECT_END()

    explicit UIDockTabStack(std::string name = "DockTabStack",
                            UIDockSpace* area = nullptr,
                            DockNodeId stackId = kInvalidDockNodeId);

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIDockTabStack>; }
    [[nodiscard]] UIDockSpace* area() const { return _area; }
    [[nodiscard]] DockNodeId   stackId() const { return _stackId; }

  private:
    UIDockSpace* _area    = nullptr;
    DockNodeId   _stackId = kInvalidDockNodeId;
};

/// DockArea leaf tab strip. TabWell drop target (insert / reorder).
/// Does not own `FDockContext`.
struct YA_GUI_API UIDockTabWell : public UITabBar
{
    explicit UIDockTabWell(std::string name = "DockTabWell",
                           UIDockSpace* area = nullptr,
                           DockNodeId stackId = kInvalidDockNodeId);

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIDockTabWell>; }
    [[nodiscard]] UIDockSpace* area() const { return _area; }
    [[nodiscard]] DockNodeId   stackId() const { return _stackId; }

  private:
    UIDockSpace* _area    = nullptr;
    DockNodeId   _stackId = kInvalidDockNodeId;
};

} // namespace ya
