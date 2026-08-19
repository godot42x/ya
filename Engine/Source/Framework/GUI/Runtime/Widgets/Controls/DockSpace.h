#pragma once

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/Controls/DockNode.h"

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace ya
{

struct UIContainer;
struct UISplitPane;
struct UITabBar;

/// DockSpace (editor-parity P7, minimal subset): a three-zone docking area —
/// left / center / right — built from nested UISplitPanes. Each zone is a
/// tab group (UITabBar + content host). Tabs are draggable: dragging a tab
/// onto another zone moves the panel there (the tab disappears from the
/// source group and appears in the target group). No floating windows, no
/// persistence — those are later steps.
struct YA_GUI_API UIDockSpace : public UIElement
{
    explicit UIDockSpace(std::string name = "DockSpace");

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIDockSpace>; }

    /// Add a panel to a zone (its widget becomes the zone's active content
    /// when its tab is selected). `zone` is 0=left, 1=center, 2=right.
    void addPanel(const std::string& name, std::shared_ptr<UIElement> widget, int zone = 1);


    /// Zone names (scenario assertions / host integration).
    static constexpr const char* kZoneLeft   = "DockZoneLeft";
    static constexpr const char* kZoneCenter = "DockZoneCenter";
    static constexpr const char* kZoneRight  = "DockZoneRight";

    /// Payload prefix carried by tab-drag sessions.
    static constexpr const char* kDockTabPayload = "dock-tab:";

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool canAcceptDrop(const std::string& payload, const glm::vec2& logicalPoint) override;
    void onDrop(const std::string& payload, const glm::vec2& logicalPoint) override;
    void setDropHighlight(bool bHighlight) override;
    void clearTransientInputState() override;

private:
    struct FPanel
    {
        DockPanelId                   id = kInvalidDockPanelId;
        std::string                   name;
        std::shared_ptr<UIElement>    widget;
    };
    struct FTabGroup
    {
        std::string         zoneName;
        std::vector<FPanel> panels;
    };
    struct FLeafView
    {
        UITabBar* bar = nullptr;
        UIContainer* content = nullptr;
    };

    /// Rebuild a zone's tab bar from its panel list and show the selected
    /// panel's widget in the content host.
    void rebuildZone(int zoneIndex, int selectIndex = -1);
    /// Move the panel `panelName` from `srcZone` to `dstZone` (no-op when
    /// the zones are identical or the panel is not found).
    void movePanel(int srcZone, int dstZone, const std::string& panelName);
    void ensureModelLayout();
    std::shared_ptr<UIElement> materializeNode(const FDockNode& node);
    int zoneForLeaf(DockNodeId leafId) const;
    FLeafView* leafViewForZone(int zone);
    [[nodiscard]] FPanel* findPanel(int zone, const std::string& name);
    [[nodiscard]] const FPanel* findPanel(int zone, const std::string& name) const;

    FTabGroup _groups[3];
    std::unordered_map<DockNodeId, FLeafView> _leafViews;
    FDockTreeModel _model;
    DockNodeId _zoneLeafIds[3] = {kInvalidDockNodeId, kInvalidDockNodeId, kInvalidDockNodeId};
    DockPanelId _nextPanelId = 1;
    int       _dropZone = -1;
};

} // namespace ya
