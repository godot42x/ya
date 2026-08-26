#pragma once

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/Controls/DockNode.h"
#include "GUI/Widgets/Controls/DockWorkspace.h"
#include "GUI/Widgets/Theme.h"

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
/// tab splits into cardinal sub-leaves or merges into another leaf.
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
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        nlohmann::json preview = {
            {"active", hasDropPreview()},
            {"disabled", isDropPreviewDisabled()},
            {"targetLeafId", getDropPreviewTargetLeafId()},
            {"kind", isDropPreviewMerge() ? "merge" : "cardinal"},
            {"disabledReason", getDropPreviewDisabledReason()},
        };
        if (!hasDropPreview()) preview["kind"] = "none";
        node["control"] = {{"type", "dockSpace"}, {"preview", std::move(preview)}};
    }
    void paintChildren(UIFrameBuilder& builder) override;
    void paintDropPreviewOverlay(UIFrameBuilder& builder) const;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool canAcceptDrop(const std::string& payload, const glm::vec2& logicalPoint) override;
    void onDrop(const std::string& payload, const glm::vec2& logicalPoint) override;
    void setDropHighlight(bool bHighlight) override;
    /// Point-sensitive merge/split preview: resolves the highlight at the
    /// current pointer on every drag move.
    void updateDropHover(const std::string& payload, const glm::vec2& logicalPoint) override;
    void clearTransientInputState() override;
    /// Clear the active drop-preview (chooser) and detach the overlay. Used when
    /// an external drag source (e.g. a floating window tab) ends its session.
    void clearDropPreview();

    /// Theme style key (style-system Phase 2/3). When the owning tree has a
    /// theme that defines this key as an FDockSpaceStyle, paintSelf resolves
    /// the canvas + drop-preview brushes from it; otherwise the literals in
    /// paintSelf are the fallback. Empty key disables theme resolution.
    std::string _styleKey = "dock";

    [[nodiscard]] bool hasDropPreview() const { return _preview.has_value(); }
    [[nodiscard]] bool isDropPreviewDisabled() const { return _preview.has_value() && _preview->bDisabled; }
    [[nodiscard]] bool isDropPreviewChooser() const { return _preview.has_value() && _preview->bChooser; }
    [[nodiscard]] DockNodeId getDropPreviewTargetLeafId() const { return _preview ? _preview->targetLeafId : kInvalidDockNodeId; }
    [[nodiscard]] const std::string& getDropPreviewDisabledReason() const;
    [[nodiscard]] bool isDropPreviewMerge() const { return _preview.has_value() && _preview->bMerge; }
    /// Copy of the current drop-preview (nullopt if none). Used by external drag
    /// sources (floating window tab) to persist the last valid chooser.
    /// Drop-preview resolved at the current pointer: either a specific chooser
    /// block (bMerge / side) or the dimmed chooser mode (bChooser) shown while
    /// the pointer is over a leaf but not yet on a block.
    struct FDropPreview
    {
        DockNodeId targetLeafId = kInvalidDockNodeId;
        /// When set, the drop targets a floating window (merge as a new tab)
        /// rather than a dock-tree leaf. Valid only when bMerge is true.
        FDockFloatingWindowId targetFloatingId = kInvalidFloatingWindowId;
        DockPanelId panelId = kInvalidDockPanelId;
        EDockCardinalSide side = EDockCardinalSide::West;
        Rect2D rect{};
        std::string prompt;
        bool bMerge = false;
        /// When true (with bMerge), the drop merges the panel into the target
        /// leaf's TAB GROUP (imgui-style "drop on a tab to merge"), rather than
        /// the center merge band. The overlay highlights the leaf's tab bar.
        bool bTabBar = false;
        /// True while the pointer is over the target leaf but not yet over a
        /// specific chooser block: render the chooser blocks (center + 4
        /// cardinals) without activating any side. Once the pointer enters a
        /// block, bChooser is cleared and the matching side/merge is active.
        bool bChooser = false;
        bool bDisabled = false;
        std::string disabledReason;
    };

    [[nodiscard]] std::optional<FDropPreview> dropPreview() const { return _preview; }
    /// Replace the current drop-preview without re-resolving (e.g. to keep the
    /// last chooser visible while the pointer is over empty space).
    void setDropPreview(const FDropPreview& preview);
    /// Resolve the drop-preview for a payload at a point (used by external drop
    /// targets such as floating windows to decide whether a drop is accepted).
    [[nodiscard]] std::optional<FDropPreview> dropPreviewFor(const std::string& payload,
                                                             const glm::vec2& logicalPoint) const;

private:
    struct FLeafView
    {
        DockNodeId  leafId = kInvalidDockNodeId;
        UIContainer* root   = nullptr;
        UITabBar* bar = nullptr;
        UIContainer* content = nullptr;
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
    void syncPreviewOverlay();

    std::unordered_map<DockNodeId, FLeafView> _leafViews;
    std::optional<FDropPreview> _preview;
    std::shared_ptr<UIElement> _previewOverlay;
    std::shared_ptr<UIDockWorkspace> _ws;
};

} // namespace ya
