#include "GUI/Widgets/Controls/Expander.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "GUI/Widgets/Controls/DisclosureChrome.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/UITypeIds.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Render/Resources/FontManager.h"

#include <algorithm>

namespace ya
{

namespace
{

// Matches hitTestLayoutRect's inclusive edges, so the hover region and the hit
// region agree at the boundary pixels.
bool rectContains(const Rect2D& rect, const glm::vec2& point)
{
    return point.x >= rect.pos.x && point.x <= rect.pos.x + rect.extent.x &&
           point.y >= rect.pos.y && point.y <= rect.pos.y + rect.extent.y;
}

} // namespace

UIExpanderHeader::UIExpanderHeader(std::string name)
    : UIOverlay(std::move(name))
{
}

UIExpander::UIExpander(std::string name)
    : UIElement(std::move(name), "expander")
{
    _hitFilter   = EWidgetHitFilter::Stop;
    _focusPolicy = EWidgetFocusPolicy::Focusable;
    bindHostLayout(_bodyLayout);
    _bodyLayout.setDirection(EWidgetBoxLayout::Vertical);
    applyFramedDefaults();
}

void UIExpander::applyFramedDefaults()
{
    if (_bFramed) {
        _headerHeight = 24.0f;
        _indent       = 0.0f;
        _bodyLayout.setPadding({6.0f, 6.0f});
        setStyleKey("expander.header");
    }
    else {
        _headerHeight = 22.0f;
        _indent       = 16.0f;
        _bodyLayout.setPadding({4.0f, 6.0f});
        setStyleKey("expander");
    }
    _bodyLayout.setSpacing(6.0f);
}

void UIExpander::setFramed(bool framed)
{
    if (_bFramed == framed) {
        return;
    }
    _bFramed = framed;
    applyFramedDefaults();
    invalidateProperty(EUIPropertyImpact::Layout);
}

void UIExpander::setTitle(std::string value)
{
    if (_title == value) {
        return;
    }
    _title = std::move(value);
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UIExpander::setIcon(FBrush icon)
{
    if (_icon == icon) {
        return;
    }
    _icon = std::move(icon);
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UIExpander::setDisclosureKind(EDisclosureKind kind)
{
    if (_disclosure.kind == kind) {
        return;
    }
    const bool bLayout = showsDisclosureButton(_disclosure) != (kind != EDisclosureKind::Hidden);
    _disclosure.kind = kind;
    invalidateProperty(bLayout ? EUIPropertyImpact::Layout : EUIPropertyImpact::Paint);
}

void UIExpander::setDisclosureSpec(FDisclosureSpec spec)
{
    if (_disclosure == spec) {
        return;
    }
    const bool bLayout = showsDisclosureButton(_disclosure) != showsDisclosureButton(spec);
    _disclosure = std::move(spec);
    invalidateProperty(bLayout ? EUIPropertyImpact::Layout : EUIPropertyImpact::Paint);
}

void UIExpander::setDisclosureGlyphs(std::string collapsed, std::string expanded)
{
    FDisclosureSpec spec = _disclosure;
    spec.kind            = EDisclosureKind::Glyph;
    spec.collapsedGlyph  = std::move(collapsed);
    spec.expandedGlyph   = std::move(expanded);
    setDisclosureSpec(std::move(spec));
}

void UIExpander::setDisclosureImages(FBrush collapsed, FBrush expanded)
{
    FDisclosureSpec spec = _disclosure;
    spec.kind            = EDisclosureKind::Image;
    spec.collapsedImage  = std::move(collapsed);
    spec.expandedImage   = std::move(expanded);
    setDisclosureSpec(std::move(spec));
}

void UIExpander::setExpanded(bool expanded)
{
    if (_bExpanded == expanded) {
        return;
    }
    _bExpanded = expanded;
    invalidateProperty(EUIPropertyImpact::Layout);
    if (_onExpandedChanged) {
        _onExpandedChanged(_bExpanded);
    }
}

UIExpanderHeader* UIExpander::findHeaderActions()
{
    for (const UIElementRef& child : getChildren()) {
        if (auto* header = dynamic_cast<UIExpanderHeader*>(child.get())) {
            return header;
        }
    }
    return nullptr;
}

const UIExpanderHeader* UIExpander::findHeaderActions() const
{
    for (const UIElementRef& child : getChildren()) {
        if (auto* header = dynamic_cast<const UIExpanderHeader*>(child.get())) {
            return header;
        }
    }
    return nullptr;
}

UIExpanderHeader& UIExpander::getHeaderActions()
{
    if (UIExpanderHeader* existing = findHeaderActions()) {
        return *existing;
    }
    // Through the registry so the region carries its type id: document
    // round-trips keep the region (and its children) as header chrome.
    UIElementRef created = UITypeRegistry::instance().createInstance(kTypeIdExpanderHeader);
    YA_CORE_ASSERT(created, "UIExpanderHeader is not registered in UITypeRegistry");
    UIExpanderHeader& ref = *static_cast<UIExpanderHeader*>(created.get());
    addDetachedChild(std::move(created));
    return ref;
}

Rect2D UIExpander::headerActionsRect() const
{
    const Rect2D header = headerRect();
    FDisclosureLeading leading = layoutDisclosureLeading(header,
                                                               _arrowWidth,
                                                               showsDisclosureButton(_disclosure),
                                                               brushHasIcon(_icon));
    return {
        .pos    = {leading.title.pos.x, header.pos.y},
        .extent = {std::max(0.0f, header.pos.x + header.extent.x - leading.title.pos.x), header.extent.y},
    };
}

float UIExpander::headerActionsLeft() const
{
    const UIExpanderHeader* actions = findHeaderActions();
    if (!actions) {
        return _layoutRect.pos.x + _layoutRect.extent.x;
    }
    const Rect2D region = headerActionsRect();
    float       left    = region.pos.x + region.extent.x;
    for (const UIElementRef& child : actions->getChildren()) {
        if (child->isVisibleForRender() && child->participatesInLayout()) {
            left = std::min(left, child->getLayoutRect().pos.x);
        }
    }
    return left;
}

void UIExpander::toggleExpanded()
{
    setExpanded(!_bExpanded);
}

Rect2D UIExpander::headerRect() const
{
    return {
        .pos    = _layoutRect.pos,
        .extent = {_layoutRect.extent.x, std::min(_headerHeight, _layoutRect.extent.y)},
    };
}

Rect2D UIExpander::bodyContentRect() const
{
    return {
        .pos    = {_layoutRect.pos.x + _indent, _layoutRect.pos.y + _headerHeight},
        .extent = {
            std::max(0.0f, _layoutRect.extent.x - _indent),
            std::max(0.0f, _layoutRect.extent.y - _headerHeight),
        },
    };
}

Rect2D UIExpander::arrowRect() const
{
    return layoutDisclosureLeading(headerRect(),
                                   _arrowWidth,
                                   showsDisclosureButton(_disclosure),
                                   brushHasIcon(_icon))
        .button;
}

bool UIExpander::headerContains(const glm::vec2& point) const
{
    const Rect2D header = headerRect();
    return point.x >= header.pos.x &&
           point.x <= header.pos.x + header.extent.x &&
           point.y >= header.pos.y &&
           point.y <= header.pos.y + header.extent.y;
}

void UIExpander::collapseChildren()
{
    // Keep collapsed children out of the header and body hit regions. A
    // zero-extent rect at the origin would still contain its own origin
    // (hitTestLayoutRect uses inclusive edges) and steal the top-left pixel.
    // The header actions region is header chrome, not body: it keeps its rect.
    const Rect2D collapsed{
        .pos    = {_layoutRect.pos.x, _layoutRect.pos.y + _layoutRect.extent.y + 1.0f},
        .extent = {0.0f, 0.0f},
    };
    UIExpanderHeader* const actions = findHeaderActions();
    for (UIElement* child : getChildrenInPaintOrder()) {
        if (child == actions) {
            continue;
        }
        child->layoutAssigned(collapsed);
    }
}

void UIExpander::applyAssignedLayout(const Rect2D& rect)
{
    setLayoutRect(rect);
    if (UIExpanderHeader* actions = findHeaderActions()) {
        actions->layoutAssigned(headerActionsRect());
    }
    if (!_bExpanded) {
        collapseChildren();
        return;
    }
    _bodyLayout.arrange(*this, bodyContentRect());
}

glm::vec2 UIExpander::computeDesiredSize() const
{
    glm::vec2 body{0.0f, 0.0f};
    if (_bExpanded) {
        body = _bodyLayout.measure(*this);
        body.x += _indent;
    }
    return {std::max(body.x, 1.0f), _headerHeight + body.y};
}

std::unique_ptr<UISlot> UIExpander::createSlotForChild(UIElement& child)
{
    auto slot = _bodyLayout.createSlot(*this, child);
    // The header actions region rides the header row, not the body column:
    // the body box layout must skip it (measure + arrange), the expander
    // assigns its rect itself. Applies however the child arrived — lazy
    // `getHeaderActions()` or a document child re-attached on instantiate.
    if (dynamic_cast<UIExpanderHeader*>(&child)) {
        if (auto* box = slot->as<UIBoxSlot>()) {
            box->setParticipatesInLayout(false);
        }
    }
    return slot;
}

void UIExpander::paintSelf(UIFrameBuilder& builder)
{
    const FExpanderStyle& style  = resolvedStyle();
    const Rect2D          header = headerRect();
    const FBrush& fill = resolveVisualFill(visualChrome(style),
                                           composeVisualFlags(_bHovered,
                                                              _bPressed,
                                                              _bFocused,
                                                              !isEnabledInTree(),
                                                              false,
                                                              false,
                                                              false));
    // Framed headers keep the full-width bar. Unframed groups only hover the
    // disclosure box so nested Params/Albedo Slot do not look like another
    // CollapsingHeader when the pointer is elsewhere in the body.
    if (_bFramed && fill.tintColor.a > 0.0f) {
        // The framed key's brush owns the bar's radius + edge, so the frame is
        // part of the same surface as the fill (no parallel outline field).
        builder.addBrush(header, fill);
    }
    if (!_bFramed && style.guideColor.a > 0.0f) {
        const float x  = header.pos.x + 7.0f;
        const float y0 = header.pos.y + header.extent.y;
        builder.addLine({header.pos.x + _arrowWidth + 4.0f, y0},
                        {header.pos.x + header.extent.x, y0},
                        style.guideColor,
                        1.0f);
        if (_bExpanded && _layoutRect.extent.y > _headerHeight) {
            builder.addLine({x, y0},
                            {x, _layoutRect.pos.y + _layoutRect.extent.y},
                            style.guideColor,
                            1.0f);
        }
    }

    auto font = builder.getFont(DEFAULT_RUNTIME_FONT_NAME, style.fontSize);
    const float packH = font ? static_cast<float>(font->lineHeight) : 0.0f;
    FDisclosureLeading leading = layoutDisclosureLeading(header,
                                                               _arrowWidth,
                                                               showsDisclosureButton(_disclosure),
                                                               brushHasIcon(_icon),
                                                               14.0f,
                                                               packH);
    paintDisclosureButton(builder,
                          FDisclosurePaint{
                              .buttonRect  = leading.button,
                              .bExpanded   = _bExpanded,
                              .bHovered    = _bArrowHovered.get(),
                              .color       = style.arrowColor,
                              .hoveredFill = style.arrowHoveredFill,
                              .spec        = _disclosure,
                              .font        = font,
                          });
    if (brushHasIcon(_icon)) {
        builder.addBrush(leading.icon, _icon);
    }
    // A header actions region keeps the title clear of its rendered children;
    // an empty (or absent) region leaves the title full width.
    const float actionsLeft = headerActionsLeft();
    if (actionsLeft < leading.title.pos.x + leading.title.extent.x) {
        leading.title.extent.x = std::max(0.0f, actionsLeft - 4.0f - leading.title.pos.x);
    }
    if (font) {
        builder.addText(leading.title, _title, style.textColor, font,
                        EWidgetAlignH::Left, EWidgetAlignV::Center);
    }
}

void UIExpander::paintChildren(UIFrameBuilder& builder)
{
    if (!_bExpanded) {
        // Body is folded away, but the header actions region is header
        // chrome: its controls stay rendered (and hit-testable) while
        // collapsed.
        if (UIExpanderHeader* actions = findHeaderActions()) {
            actions->paint(builder);
        }
        return;
    }
    UIElement::paintChildren(builder);
}

bool UIExpander::hitTestSelf(const glm::vec2& logicalPoint) const
{
    return isHitTestableSelf() && headerContains(logicalPoint);
}

bool UIExpander::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && (keyEvent._keyCode == EKey::Enter || keyEvent._keyCode == EKey::Space)) {
            toggleExpanded();
            return true;
        }
        return false;
    }

    const bool bPointOnHeader = headerContains(ctx.logicalPoint);
    if (!ctx.bViaCapture && !bPointOnHeader) {
        return false;
    }

    // The disclosure marks its own hover on every pointer position we see
    // here (move/press/release); leaving the widget clears it in
    // onPointerLeave.
    _bArrowHovered = rectContains(arrowRect(), ctx.logicalPoint);

    switch (eventType) {
    case EEvent::MouseButtonPressed:
        _bPressed = true;
        if (WidgetTree* tree = getTree()) {
            tree->setFocus(this);
            tree->setPointerCapture(this);
        }
        return true;
    case EEvent::MouseButtonReleased:
        if (!_bPressed) {
            return false;
        }
        _bPressed = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        if (bPointOnHeader || ctx.bViaCapture) {
            toggleExpanded();
        }
        return true;
    case EEvent::MouseMoved:
        return true;
    default:
        return false;
    }
}

void UIExpander::clearTransientInputState()
{
    _bHovered      = false;
    _bPressed      = false;
    _bArrowHovered = false;
}

} // namespace ya
