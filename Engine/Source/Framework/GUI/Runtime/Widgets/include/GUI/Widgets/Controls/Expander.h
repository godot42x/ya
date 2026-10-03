#pragma once

#include "GUI/Layout/UIBoxLayout.h"
#include "GUI/Widgets/Brush.h"
#include "GUI/Widgets/Controls/DisclosureChrome.h"
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <string>

namespace ya
{

/// Trailing actions region of a `UIExpander` header (`engine.expander_header`).
/// A real child widget so header controls are ordinary widgets — buttons,
/// checkboxes, custom chrome — composed with standard overlay slots
/// (`hAlign(End)` pins a control to the header's right edge). The expander
/// keeps the region alive while collapsed (it is header chrome, not body) and
/// clamps its painted title while the region is occupied.
struct YA_GUI_API UIExpanderHeader final : public UIOverlay
{
    YA_REFLECT_BEGIN(UIExpanderHeader, UIOverlay)
    YA_REFLECT_END()

    explicit UIExpanderHeader(std::string name = "ExpanderHeader");

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIExpanderHeader>; }
};

/// ImGui `TreeNode`: a layout host whose children are the body. The header
/// (disclosure mark, optional icon, title) is painted by this widget.
/// Collapsed children are not measured, arranged, painted, or hit-tested.
///
/// `setFramed(true)` is ImGui `CollapsingHeader` — the same control with
/// `TreeNodeFlags_Framed` (and no extra body indent). It is not a second type.
///
/// Not `UITreeView`. TreeView is a virtualized selectable *list of data rows*
/// (Hierarchy): it paints labels itself and has no per-row child widgets.
/// This host folds *widget children* (Details sections, property groups).
///
/// Children are hit-tested first, so body controls keep their input. Header
/// clicks land here because no child covers the header. `_hitFilter` is Stop
/// so a header click does not fall through the panel. A `UIExpanderHeader`
/// child (header actions) is the exception: it rides the header row, survives
/// collapse, and its controls win the hit over the header toggle.
struct YA_GUI_API UIExpander : public UIElement, public UIStyledWidget<UIExpander, FExpanderStyle>
{
    using SlotArgs = FBoxSlotArgs;

    YA_REFLECT_BEGIN(UIExpander, UIElement)
        YA_REFLECT_FIELD(_title, .instanceEditable())
        YA_REFLECT_FIELD(_headerHeight, .instanceEditable())
        // Private composite/state below are reachable: the reflect visitor is
        // instantiated inside the class scope.
        YA_REFLECT_FIELD(_bodyLayout, .instanceEditable())
        YA_REFLECT_FIELD(_bExpanded, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FExpanderStyle)

    explicit UIExpander(std::string name = "Expander");

    std::string _title;
    FBrush            _icon;
    FDisclosureSpec   _disclosure;
    float             _headerHeight = 22.0f;
    float       _indent       = 16.0f;
    float       _arrowWidth   = 18.0f;

    VisualFlag _bHovered{*this};
    VisualFlag _bPressed{*this};
    VisualFlag _bFocused{*this};
    /// Per-control hover for the disclosure mark: the arrow box lights only
    /// while the pointer is on it, not anywhere on the header row.
    VisualFlag _bArrowHovered{*this};

    std::function<void(bool expanded)> _onExpandedChanged;

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIExpander>; }

    [[nodiscard]] UIBoxLayout&       getBoxLayout() { return _bodyLayout; }
    [[nodiscard]] const UIBoxLayout& getBoxLayout() const { return _bodyLayout; }

    /// ImGui `TreeNodeFlags_Framed`. Framed headers use the `expander.header`
    /// look and drop body indent; unframed uses `expander` and `_indent`.
    void setFramed(bool framed);
    [[nodiscard]] bool isFramed() const { return _bFramed; }

    void setTitle(std::string value);
    [[nodiscard]] const std::string& getTitle() const { return _title; }

    /// Trailing header actions region (see `UIExpanderHeader`). Created on
    /// first use — an expander without header actions carries none. Attach
    /// controls to the returned region, not to the expander; its box slot
    /// opts out of the body layout.
    [[nodiscard]] UIExpanderHeader& getHeaderActions();
    /// The live region without creating one (null until `getHeaderActions`).
    [[nodiscard]] UIExpanderHeader* findHeaderActions();
    [[nodiscard]] const UIExpanderHeader* findHeaderActions() const;

    /// Optional leading icon after the disclosure: `mark icon Name`. Empty
    /// resource means no icon. Hide the mark with
    /// `setDisclosureKind(EDisclosureKind::Hidden)` to keep only the icon.
    void setIcon(FBrush icon);
    [[nodiscard]] const FBrush& getIcon() const { return _icon; }

    void setDisclosureKind(EDisclosureKind kind);
    void setDisclosureSpec(FDisclosureSpec spec);
    void setDisclosureGlyphs(std::string collapsed, std::string expanded);
    void setDisclosureImages(FBrush collapsed, FBrush expanded = {});
    [[nodiscard]] const FDisclosureSpec& getDisclosure() const { return _disclosure; }

    void setExpanded(bool expanded);
    void toggleExpanded();
    [[nodiscard]] bool isExpanded() const { return _bExpanded; }

    void setSpacing(float value) { _bodyLayout.setSpacing(value); }
    void setPadding(glm::vec2 value) { _bodyLayout.setPadding(value); }
    [[nodiscard]] float getSpacing() const { return _bodyLayout.getSpacing(); }
    [[nodiscard]] glm::vec2 getPadding() const { return _bodyLayout.getPadding(); }

    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override
    {
        const auto& l = _bodyLayout;
        node["type"]      = "expander";
        node["expanded"]  = _bExpanded;
        node["framed"]    = _bFramed;
        node["spacing"]   = l.getSpacing();
        node["padding"]   = {{"x", l.getPadding().x}, {"y", l.getPadding().y}};
    }
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override
    {
        node["control"] = {
            {"type", "expander"},
            {"title", _title},
            {"expanded", _bExpanded},
            {"framed", _bFramed},
            {"hasIcon", !_icon.resource.empty()},
            {"disclosure", disclosureKindName(_disclosure.kind)},
        };
    }

    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    [[nodiscard]] std::unique_ptr<UISlot> createSlotForChild(UIElement& child) override;

    void paintSelf(UIFrameBuilder& builder) override;
    void paintChildren(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    [[nodiscard]] bool hitTestSelf(const glm::vec2& logicalPoint) const override;
    [[nodiscard]] bool isHoverable() const override { return true; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override
    {
        _bHovered      = false;
        _bArrowHovered = false;
    }
    void resetHoverState() override { onPointerLeave(); }
    void clearTransientInputState() override;
    void onFocusGained(bool bFromKeyboard) override { _bFocused = bFromKeyboard; }
    void onFocusLost() override { _bFocused = false; }

  protected:
    void applyAssignedLayout(const Rect2D& rect) override;

  private:
    void applyFramedDefaults();
    [[nodiscard]] Rect2D headerRect() const;
    /// Trailing region of the header row: right of the disclosure/icon mark,
    /// full header height. The painted title clamps to the region's occupied
    /// children (see `headerActionsLeft`).
    [[nodiscard]] Rect2D headerActionsRect() const;
    /// Left edge of the region's rendered children, or the header's right edge
    /// when the region is empty/absent.
    [[nodiscard]] float  headerActionsLeft() const;
    [[nodiscard]] Rect2D bodyContentRect() const;
    [[nodiscard]] Rect2D arrowRect() const;
    [[nodiscard]] bool   headerContains(const glm::vec2& point) const;
    void                 collapseChildren();

    UIBoxLayout _bodyLayout;
    bool        _bExpanded = true;
    bool        _bFramed   = false;
};

} // namespace ya
