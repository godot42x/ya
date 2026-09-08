#pragma once

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/DragDropOperation.h"

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
struct FDockContext;

/// Typed drag payload for a dock panel (UE `FTabDragDropOp` analog).
/// Drop targets `as<FDockPanelDragDropOp>()` instead of parsing a string prefix.
struct YA_GUI_API FDockPanelDragDropOp : public UIDragDropOperation
{
    static constexpr const char* kTypeId = "ya.dock.panel";
    DockPanelId panelId = kInvalidDockPanelId;

    FDockPanelDragDropOp() { typeId = kTypeId; }
    ~FDockPanelDragDropOp() override;

    static UIDragDropOperationRef make(DockPanelId panelId, std::string ghostLabel)
    {
        auto operation = std::make_shared<FDockPanelDragDropOp>();
        operation->panelId = panelId;
        operation->ghostLabel = std::move(ghostLabel);
        return operation;
    }
};

/// In-window projection of `FDockContext`'s docked tree.
/// Nested UISplitPanes + tab groups fill this widget. The context owns the
/// model, panel registry, floating records, and policy; this widget does not.
/// Torn-off windows are projected by `UIDockFloatingHost`, not here.
/// There is no fixed zone layout — the initial model is a single root leaf,
/// and dragging a tab splits into cardinal sub-leaves or merges into another leaf.
struct YA_GUI_API UIDockSpace : public UIElement, public UIStyledWidget<UIDockSpace, FDockSpaceStyle>
{
    YA_GUI_AUTHORED_STYLE_IO(FDockSpaceStyle)

    explicit UIDockSpace(std::string name = "DockSpace");
    /// Unregister the context back-pointer (FDockContext::_dockSpace) so a
    /// context that outlives this widget never hands out a dangling pointer.
    ~UIDockSpace() override;
    /// Bind the shared session this dock reads its model / registry / policy from.
    void setContext(std::shared_ptr<FDockContext> context);
    [[nodiscard]] FDockContext* context() const { return _context.get(); }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIDockSpace>; }

    /// Add a panel through the context (its widget becomes that leaf's active
    /// content when its tab is selected).
    void addPanel(const std::string& name, std::shared_ptr<UIElement> widget);

    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override
    {
        node["type"] = "singleChild";
    }
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
    void clearTransientInputState() override;
    /// Clear the active drop-preview (chooser) and detach the overlay. Used when
    /// an external drag source (e.g. a floating window tab) ends its session.
    void clearDropPreview();

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
    [[nodiscard]] std::optional<FDropPreview> dropPreviewFor(const UIDragDropOperation& operation,
                                                             const glm::vec2& logicalPoint) const;

  protected:
    void applyAssignedLayout(const Rect2D& rect) override;
    [[nodiscard]] bool assignedLayoutInputsUnchanged() const override;

private:
    friend struct FDockSpacePanelDragBehavior;
    friend struct FDockSpaceDropTargetBehavior;
    struct FLeafView
    {
        DockNodeId   leafId  = kInvalidDockNodeId;
        UIElement*   root    = nullptr;
        UITabBar*    bar     = nullptr;
        UIContainer* content = nullptr;
    };


    void rebuildProjection();
    void rebuildLeaf(DockNodeId leafId);
    void releaseMountedPanels();
    void applyLeafTabBarVisibility(DockNodeId leafId);
    void openLeafTabBarMenu(DockNodeId leafId, const glm::vec2& pos);
    void graftPanelIntoContent(UIContainer& content, const UIElementRef& panel);
    /// Same-leaf drop that is not a tab-bar reorder: select + graft, never split.
    void activateDraggedPanel(DockPanelId panelId);
    std::shared_ptr<UIElement> materializeNode(const FDockNode& node);
    FLeafView* leafViewForLeaf(DockNodeId leafId);
    [[nodiscard]] const FLeafView* leafViewForLeaf(DockNodeId leafId) const;
    [[nodiscard]] size_t tabInsertIndexAt(DockNodeId leafId, const glm::vec2& logicalPoint) const;
    [[nodiscard]] std::optional<FDropPreview> resolveDropPreview(const glm::vec2& logicalPoint,
                                                                 DockPanelId panelId) const;
    void clearPreview();
    void syncPreviewOverlay();

    std::unordered_map<DockNodeId, FLeafView> _leafViews;
    std::optional<FDropPreview> _preview;
    std::shared_ptr<UIElement> _previewOverlay;
    std::shared_ptr<FDockContext> _context;
    bool _bRebuildingProjection = false;
};

} // namespace ya
