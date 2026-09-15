#pragma once

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/DockSpace/DockDropTarget.h"
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
    DockPanelId   panelId       = kInvalidDockPanelId;
    FDockContext* sourceContext = nullptr;
    uint32_t      sourceWindowId = 0;

    FDockPanelDragDropOp() { typeId = kTypeId; }
    ~FDockPanelDragDropOp() override;

    static UIDragDropOperationRef make(DockPanelId   panelId,
                                       std::string   ghostLabel,
                                       FDockContext* sourceContext = nullptr)
    {
        auto operation = std::make_shared<FDockPanelDragDropOp>();
        operation->panelId        = panelId;
        operation->ghostLabel     = std::move(ghostLabel);
        operation->sourceContext  = sourceContext;
        operation->sourceWindowId = sourceContext ? sourceContext->hostWindowId : 0;
        return operation;
    }
};

/// In-window projection of `FDockContext`'s docked tree.
/// Nested UISplitPanes + tab groups fill this widget. The context owns the
/// model, panel registry, floating records, and policy; this widget does not.
/// Torn-off windows are projected by `UIDockFloatingHost`, not here.
/// There is no fixed zone layout — the initial model is a single root stack,
/// and dragging a tab splits into cardinal sub-stacks or merges into another stack.
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

    /// Add a panel through the context (its widget becomes that stack's active
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
            {"targetStackId", getDropPreviewTargetStackId()},
            {"kind", hasDropPreview() ? dockDropTargetKindName(getDropPreviewKind()) : "none"},
            {"disabledReason", getDropPreviewDisabledReason()},
        };
        node["control"] = {{"type", "dockSpace"}, {"preview", std::move(preview)}};
    }
    void paintChildren(UIFrameBuilder& builder) override;
    void paintDropPreviewOverlay(UIFrameBuilder& builder) const;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void clearTransientInputState() override;
    /// Clear the active drop-preview (chooser) and detach the overlay. Used when
    /// an external drag source (e.g. a floating window tab) ends its session.
    void clearDropPreview();

    /// Visual overlay for a resolved `FDockDropTarget`. Geometry and prompt
    /// stay here; commit semantics live on `target.kind`.
    struct FDropPreview
    {
        FDockDropTarget target;
        DockPanelId     panelId = kInvalidDockPanelId;
        Rect2D          rect{};
        std::string     prompt;
        bool            bDisabled = false;
        std::string     disabledReason;
    };

    [[nodiscard]] bool hasDropPreview() const { return _preview.has_value(); }
    [[nodiscard]] bool isDropPreviewDisabled() const { return _preview.has_value() && _preview->bDisabled; }
    [[nodiscard]] bool isDropPreviewChooser() const
    {
        return _preview.has_value() && _preview->target.isPreviewOnly();
    }
    [[nodiscard]] DockNodeId getDropPreviewTargetStackId() const
    {
        return _preview ? _preview->target.stackId : kInvalidDockNodeId;
    }
    [[nodiscard]] DockNodeId getDropPreviewTargetLeafId() const
    {
        return getDropPreviewTargetStackId();
    }
    [[nodiscard]] EDockDropTargetKind getDropPreviewKind() const
    {
        return _preview ? _preview->target.kind : EDockDropTargetKind::NoTarget;
    }
    [[nodiscard]] const std::string& getDropPreviewDisabledReason() const;
    [[nodiscard]] bool isDropPreviewMerge() const
    {
        return _preview.has_value() && _preview->target.isMerge();
    }

    [[nodiscard]] std::optional<FDropPreview> dropPreview() const { return _preview; }
    /// Replace the current drop-preview without re-resolving (e.g. to keep the
    /// last chooser visible while the pointer is over empty space).
    void setDropPreview(const FDropPreview& preview);
    /// Resolve the drop-preview for a payload at a point (used by external drop
    /// targets such as floating windows to decide whether a drop is accepted).
    [[nodiscard]] std::optional<FDropPreview> dropPreviewFor(const UIDragDropOperation& operation,
                                                             const glm::vec2& logicalPoint) const;
    /// Leaf Well/Stack drop targets call these so Area owns overlay + commit.
    void hoverDrop(const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
    void applyDrop(const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
    /// Re-apply leaf tab-well / hide-affordance visibility from the model.
    /// Page-role stacks never show an inner well (chrome owns those tabs).
    void syncTabBarVisibility();

  protected:
    void applyAssignedLayout(const Rect2D& rect) override;
    [[nodiscard]] bool assignedLayoutInputsUnchanged() const override;

private:
    friend struct FDockSpacePanelDragBehavior;
    /// Visual projection of one StackNode: tab well + active content.
    struct FDockStackView
    {
        DockNodeId   stackId = kInvalidDockNodeId;
        UIElement*   root    = nullptr;
        UITabBar*    well    = nullptr;
        UIContainer* content = nullptr;
        UIElement*   hideAffordance = nullptr;
    };


    void rebuildProjection();
    void rebuildStack(DockNodeId stackId);
    void releaseMountedPanels();
    void applyStackTabBarVisibility(DockNodeId stackId);
    void openStackTabBarMenu(DockNodeId stackId, const glm::vec2& pos);
    void graftPanelIntoContent(UIContainer& content, const UIElementRef& panel);
    /// Same-stack drop on a chooser split block splits that stack.
    void activateDraggedPanel(DockPanelId panelId);
    std::shared_ptr<UIElement> materializeNode(const FDockNode& node);
    FDockStackView* stackViewFor(DockNodeId stackId);
    [[nodiscard]] const FDockStackView* stackViewFor(DockNodeId stackId) const;
    [[nodiscard]] size_t tabInsertIndexAt(DockNodeId stackId, const glm::vec2& logicalPoint) const;
    [[nodiscard]] const FDockStackView* focusStackAt(const glm::vec2& logicalPoint,
                                                   const FDockNode* sourceStack) const;
    [[nodiscard]] std::optional<FDropPreview> resolveFloatingWell(const glm::vec2& logicalPoint,
                                                                  DockPanelId      panelId,
                                                                  bool             bImport) const;
    [[nodiscard]] std::optional<FDropPreview> resolveTabWell(const FDockStackView& focus,
                                                             const glm::vec2&  logicalPoint,
                                                             DockPanelId       panelId) const;
    [[nodiscard]] std::optional<FDropPreview> resolveTabStack(const FDockStackView& focus,
                                                              const glm::vec2&  logicalPoint,
                                                              DockPanelId       panelId,
                                                              const FDockNode*  sourceStack) const;
    [[nodiscard]] std::optional<FDropPreview> resolveDropPreview(const glm::vec2& logicalPoint,
                                                                 DockPanelId panelId,
                                                                 bool bImport = false) const;
    void clearPreview();
    void syncPreviewOverlay();

    std::unordered_map<DockNodeId, FDockStackView> _stackViews;
    std::unordered_map<DockNodeId, UISplitPane*>   _splitViews;
    std::optional<FDropPreview> _preview;
    std::shared_ptr<UIElement> _previewOverlay;
    std::shared_ptr<FDockContext> _context;
    bool _bRebuildingProjection = false;
};

} // namespace ya
