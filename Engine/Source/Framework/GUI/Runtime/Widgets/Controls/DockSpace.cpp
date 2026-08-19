#include "GUI/Widgets/Controls/DockSpace.h"


#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

UIDockSpace::UIDockSpace(std::string name)
    : UIElement(std::move(name))
{
    _hitFilter = EWidgetHitFilter::Stop;

    // Three-zone layout: split(left | split(center | right)).
    _groups[0].zoneName = kZoneLeft;
    _groups[1].zoneName = kZoneCenter;
    _groups[2].zoneName = kZoneRight;
}

void UIDockSpace::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UIDockSpace::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect);

    // Build lazily on first layout (needs a tree for attach).
    if (getChildren().empty() && getTree()) {
        WidgetTree& tree = *getTree();

        auto outerSplit = std::make_shared<UISplitPane>("DockOuterSplit");
        outerSplit->setSplitRatio(0.22f);
        outerSplit->setMinFirstExtent(120.0f);
        outerSplit->setMinSecondExtent(200.0f);
        addDetachedChild(outerSplit);

        auto innerSplit = std::make_shared<UISplitPane>("DockInnerSplit");
        innerSplit->setSplitRatio(0.78f);
        innerSplit->setMinFirstExtent(200.0f);
        innerSplit->setMinSecondExtent(120.0f);

        // Three panes: left zone, then inner split holding center + right.
        auto leftPane = std::make_shared<UIContainer>(kZoneLeft);
        leftPane->setDirection(EWidgetBoxLayout::Vertical);
        leftPane->setSpacing(0.0f);
        outerSplit->addDetachedChild(leftPane);

        auto rightPane = std::make_shared<UIContainer>(kZoneRight);
        rightPane->setDirection(EWidgetBoxLayout::Vertical);
        rightPane->setSpacing(0.0f);

        auto centerPane = std::make_shared<UIContainer>(kZoneCenter);
        centerPane->setDirection(EWidgetBoxLayout::Vertical);
        centerPane->setSpacing(0.0f);

        innerSplit->addDetachedChild(centerPane);
        innerSplit->addDetachedChild(rightPane);
        outerSplit->addDetachedChild(innerSplit);

        UIContainer* panes[3] = {leftPane.get(), centerPane.get(), rightPane.get()};
        for (int zone = 0; zone < 3; ++zone) {
            FTabGroup& group = _groups[zone];

            auto bar = std::make_shared<UITabBar>(std::format("DockTabBar{}", zone));
            bar->_bDraggableTabs = true;
            bar->_emptyPlaceholder = std::format("{} (drop tabs here)", group.zoneName);
            bar->_onTabDragBegin = [this, zone](int index, const std::string& label)
            {
                if (index < 0 || index >= static_cast<int>(_groups[zone].panels.size())) {
                    return;
                }
                if (WidgetTree* tree = getTree()) {
                    tree->beginDrag(this,
                                    std::string(kDockTabPayload) + std::to_string(zone) + ":" + label,
                                    label);
                }
            };
            panes[zone]->addDetachedChild(bar);

            auto content = std::make_shared<UIContainer>(std::format("DockContent{}", zone));
            // Stretch to fill the zone's remaining height.
            panes[zone]->setStretchLastChild(true);
            panes[zone]->addDetachedChild(content);
            content->setStretchLastChild(true);

            group.bar     = bar.get();
            group.content = content.get();
        }

        // Panels added before the first layout (addPanel ran when the bars
        // did not exist yet) must materialize now.
        for (int zone = 0; zone < 3; ++zone) {
            if (!_groups[zone].panels.empty() && _groups[zone].bar->getChildren().empty()) {
                rebuildZone(zone, 0);
            }
        }
    }

    for (UIElement* child : getChildrenInPaintOrder()) {
        if (child->participatesInLayout()) {
            child->layoutAssigned(rect);
            break;
        }
    }
}

void UIDockSpace::paintSelf(UIFrameBuilder& builder)
{
    if (_dropZone >= 0) {
        // Highlight the zone that would receive the dragged tab.
        builder.addRectOutline(_layoutRect, {0.24f, 0.46f, 0.82f, 1.0f}, 2.0f);
    }
}

void UIDockSpace::addPanel(const std::string& name, std::shared_ptr<UIElement> widget, int zone)
{
    if (zone < 0 || zone > 2) {
        zone = 1;
    }
    FTabGroup& group = _groups[zone];
    group.panels.push_back({name, std::move(widget)});
    if (group.bar) {
        rebuildZone(zone, static_cast<int>(group.panels.size()) - 1);
    }
}

void UIDockSpace::rebuildZone(int zoneIndex, int selectIndex)
{
    FTabGroup& group = _groups[zoneIndex];
    if (!group.bar || !group.content) {
        return;
    }
    // Detach the previous content widget (it moves to the tab's panel list).
    if (WidgetTree* tree = getTree()) {
        for (UIElement* child : group.content->getChildrenInPaintOrder()) {
            if (child->participatesInLayout()) {
                tree->detach(*child);
                break;
            }
        }
    }
    // Clear the bar and re-add tabs.
    const int tabCount = static_cast<int>(group.bar->getChildren().size());
    for (int i = tabCount - 1; i >= 0; --i) {
        group.bar->removeTab(i);
    }
    for (const FPanel& panel : group.panels) {
        group.bar->addTab(panel.name);
    }
    int selected = selectIndex;
    if (selected < 0 || selected >= static_cast<int>(group.panels.size())) {
        selected = group.panels.empty() ? -1 : 0;
    }
    if (selected >= 0) {
        // syncSelectedTab only moves the highlight; selectTab would fire the
        // previous rebuild's _onTabSelected (double-add / wrong-panel race).
        // The content is attached exactly once, right here.
        group.bar->syncSelectedTab(selected);
        group.content->addDetachedChild(group.panels[static_cast<size_t>(selected)].widget);
    }
    // Wire selection: show the selected panel's widget.
    group.bar->_onTabSelected = [this, zoneIndex](int index)
    {
        FTabGroup& g = _groups[zoneIndex];
        if (!g.content || index < 0 || index >= static_cast<int>(g.panels.size())) {
            return;
        }
        if (WidgetTree* tree = getTree()) {
            for (UIElement* child : g.content->getChildrenInPaintOrder()) {
                if (child->participatesInLayout()) {
                    tree->detach(*child);
                    break;
                }
            }
        }
        g.content->addDetachedChild(g.panels[static_cast<size_t>(index)].widget);
    };
    markLayoutDirty();
}

void UIDockSpace::movePanel(int srcZone, int dstZone, const std::string& panelName)
{
    if (srcZone == dstZone || srcZone < 0 || srcZone > 2 || dstZone < 0 || dstZone > 2) {
        return;
    }
    FTabGroup& src = _groups[srcZone];
    FTabGroup& dst = _groups[dstZone];
    auto it = std::find_if(src.panels.begin(), src.panels.end(),
                           [&](const FPanel& p) { return p.name == panelName; });
    if (it == src.panels.end()) {
        return;
    }
    FPanel moved = *it;
    src.panels.erase(it);
    dst.panels.push_back(std::move(moved));
    rebuildZone(srcZone);
    rebuildZone(dstZone, static_cast<int>(dst.panels.size()) - 1);
}

bool UIDockSpace::canAcceptDrop(const std::string& payload, const glm::vec2& logicalPoint)
{
    return payload.rfind(kDockTabPayload, 0) == 0;
}

void UIDockSpace::onDrop(const std::string& payload, const glm::vec2& logicalPoint)
{
    _dropZone = -1;
    markPaintDirty();
    // payload: dock-tab:<srcZone>:<label>
    const std::string body  = payload.substr(std::char_traits<char>::length(kDockTabPayload));
    const size_t      sep   = body.find(':');
    if (sep == std::string::npos) {
        return;
    }
    const int         srcZone = std::atoi(body.substr(0, sep).c_str());
    const std::string label   = body.substr(sep + 1);
    // Determine the target zone from the drop point's horizontal position.
    const float t = (logicalPoint.x - _layoutRect.pos.x) / std::max(_layoutRect.extent.x, 1.0f);
    int dstZone = t < 0.25f ? 0 : (t > 0.82f ? 2 : 1);
    movePanel(srcZone, dstZone, label);
}

void UIDockSpace::setDropHighlight(bool bHighlight)
{
    markPaintDirty();
}

void UIDockSpace::clearTransientInputState()
{
    _dropZone = -1;
}

bool UIDockSpace::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    return false; // tabs and splits handle their own input
}

} // namespace ya
