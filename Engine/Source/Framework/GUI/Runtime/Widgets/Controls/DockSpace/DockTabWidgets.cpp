#include "GUI/Widgets/Controls/DockSpace/DockTabStack.h"

#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/UIBehavior.h"

namespace ya
{

namespace
{

UIDockSpace* dockAreaOf(UIElement& owner)
{
    if (auto* space = dynamic_cast<UIDockSpace*>(&owner)) {
        return space;
    }
    if (auto* stack = dynamic_cast<UIDockTabStack*>(&owner)) {
        return stack->area();
    }
    if (auto* well = dynamic_cast<UIDockTabWell*>(&owner)) {
        return well->area();
    }
    return nullptr;
}

} // namespace

void installDockDropTarget(UIElement& widget)
{
    auto drop = std::make_shared<UIDropTargetBehavior>();
    drop->canAccept = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
    {
        UIDockSpace* area = dockAreaOf(owner);
        if (!area) {
            return false;
        }
        auto preview = area->dropPreviewFor(operation, logicalPoint);
        return preview.has_value() && !preview->bDisabled && preview->target.commitsDrop();
    };
    drop->canPreview = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
    {
        UIDockSpace* area = dockAreaOf(owner);
        if (!area) {
            return false;
        }
        auto preview = area->dropPreviewFor(operation, logicalPoint);
        return preview.has_value() && !preview->bDisabled;
    };
    drop->handleDrop = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
    {
        if (UIDockSpace* area = dockAreaOf(owner)) {
            area->applyDrop(operation, logicalPoint);
        }
    };
    drop->setHighlightState = [](UIElement& owner, bool bHighlight)
    {
        if (!bHighlight) {
            if (UIDockSpace* area = dockAreaOf(owner)) {
                area->clearDropPreview();
            }
        }
    };
    drop->updateHover = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
    {
        if (UIDockSpace* area = dockAreaOf(owner)) {
            area->hoverDrop(operation, logicalPoint);
        }
    };
    widget.addBehavior(drop);
}

UIDockTabStack::UIDockTabStack(std::string name, UIDockSpace* area, DockNodeId stackId)
    : UIOverlay(std::move(name))
    , _area(area)
    , _stackId(stackId)
{
    installDockDropTarget(*this);
}

UIDockTabWell::UIDockTabWell(std::string name, UIDockSpace* area, DockNodeId stackId)
    : UITabBar(std::move(name))
    , _area(area)
    , _stackId(stackId)
{
    installDockDropTarget(*this);
}

} // namespace ya
