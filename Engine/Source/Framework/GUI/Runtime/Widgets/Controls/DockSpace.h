#pragma once

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/Controls/DockNode.h"
#include "GUI/Widgets/Controls/DockWorkspace.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

namespace ya
{

struct UIContainer;
struct UISplitPane;
struct UITabBar;
struct UIDockWorkspace;

/// DockSpace: a full nested dock tree (FDockTreeModel) projected into nested
/// UISplitPanes with tab groups. There is no fixed zone layout — the initial
/// model is a single root leaf holding all registered panels, and dragging a
/// tab splits into cardinal/corner sub-leaves or merges into another leaf.
/// No floating windows or persistence yet.
struct YA_GUI_API UIDockSpace : public UIElement
{
    explicit UIDockSpace(std::string name = "DockSpace");
    /// Bind the shared workspace this dock reads its model / registry / policy from.
    void setWorkspace(std::shared_ptr<UIDockWorkspace> ws);
    [[nodiscard]] UIDockWorkspace* workspace() const { return _ws.get(); }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIDockSpace>; }

    /// Add a panel through the workspace (its widget becomes that leaf's active
    /// content when its tab is selected).
    void addPanel(const std::string& name, std::shared_ptr<UIElement> widget);

    /// Payload prefix carried by tab-drag sessions.
    static constexpr const char* kDockPanelPayload = "dock-panel:";

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    void paintSelf(UIFrameBuilder& builder) override;
    void paintChildren(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool canAcceptDrop(const std::string& payload, const glm::vec2& logicalPoint) override;
    void onDrop(const std::string& payload, const glm::vec2& logicalPoint) override;
    void setDropHighlight(bool bHighlight) override;
    void clearTransientInputState() override;

    [[nodiscard]] bool hasDropPreview() const { return _preview.has_value(); }
    [[nodiscard]] bool isDropPreviewDisabled() const { return _preview.has_value() && _preview->bDisabled; }
    [[nodiscard]] DockNodeId getDropPreviewTargetLeafId() const { return _preview ? _preview->targetLeafId : kInvalidDockNodeId; }
    [[nodiscard]] const std::string& getDropPreviewDisabledReason() const;
    [[nodiscard]] bool isDropPreviewCorner() const { return _preview.has_value() && _preview->bCorner; }
    [[nodiscard]] bool isDropPreviewMerge() const { return _preview.has_value() && _preview->bMerge; }

private:
    struct FLeafView
    {
        DockNodeId  leafId = kInvalidDockNodeId;
        UIContainer* root   = nullptr;
        UITabBar* bar = nullptr;
        UIContainer* content = nullptr;
    };
    struct FDropPreview
    {
        DockNodeId targetLeafId = kInvalidDockNodeId;
        DockPanelId panelId = kInvalidDockPanelId;
        EDockCorner corner = EDockCorner::NorthWest;
        EDockCardinalSide side = EDockCardinalSide::West;
        Rect2D rect{};
        Rect2D emptyLeafRect{};
        bool bCorner = false;
        bool bMerge = false;
        bool bDisabled = false;
        std::string disabledReason;
    };

    void rebuildProjection();
    void rebuildLeaf(DockNodeId leafId);
    std::shared_ptr<UIElement> materializeNode(const FDockNode& node);
    FLeafView* leafViewForLeaf(DockNodeId leafId);
    [[nodiscard]] const FLeafView* leafViewForLeaf(DockNodeId leafId) const;
    [[nodiscard]] std::optional<FDropPreview> resolveDropPreview(const glm::vec2& logicalPoint,
                                                                 DockPanelId panelId) const;
    [[nodiscard]] bool parsePanelPayload(const std::string& payload, DockPanelId& panelId) const;
    void clearPreview();

    std::unordered_map<DockNodeId, FLeafView> _leafViews;
    std::optional<FDropPreview> _preview;
    std::shared_ptr<UIDockWorkspace> _ws;
};

} // namespace ya
