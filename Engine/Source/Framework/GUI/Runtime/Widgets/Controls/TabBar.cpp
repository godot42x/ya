#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/WidgetTree.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace ya
{

namespace
{

constexpr float kTabCloseSize = 12.0f;
constexpr float kTabClosePad  = 3.0f;
constexpr uint64_t kTabDoubleClickMs = 400;
constexpr float    kTabDoubleClickSlop = 6.0f;

bool containsPoint(const Rect2D& rect, const glm::vec2& point)
{
    return point.x >= rect.pos.x && point.x < rect.pos.x + rect.extent.x &&
           point.y >= rect.pos.y && point.y < rect.pos.y + rect.extent.y;
}

} // namespace

Rect2D UITabButton::closeHitRect() const
{
    if (!_bClosable || _layoutRect.extent.x <= 0.0f) {
        return {};
    }
    return Rect2D{
        .pos    = {_layoutRect.pos.x + _layoutRect.extent.x - kTabClosePad - kTabCloseSize,
                   _layoutRect.pos.y + (_layoutRect.extent.y - kTabCloseSize) * 0.5f},
        .extent = {kTabCloseSize, kTabCloseSize},
    };
}

void UITabButton::paintSelf(UIFrameBuilder& builder)
{
    // Theme resolution (style-system Phase 3). Layout level: the style's
    // padding feeds computeDesiredSize, so an edit to this tab's style must
    // re-measure (a Paint-only edge would leave the old geometry). Absent
    // key/theme → default-constructed FTabStyle is the framework fallback
    // (Phase 3 cleanup: no bare fields).
    const FTabStyle& style = resolvedStyle(ReactiveBase::EDirtyLevel::Layout);

    builder.addBrush(_layoutRect,
                     resolveVisualFill(visualChrome(style),
                                       composeVisualFlags(_bHovered, false, false, !isEnabled(), _bSelected, false, false)));
    if (_bSelected) {
        // Selected tab reads as "connected to the content below": the fill is
        // the editor-chrome base, with a thin accent bar along the top edge.
        // Drawing a slightly taller rect than our own (the bar's padding is
        // above us) would look connected, but the bar clips children, so keep
        // the fill inside this button and let the bar's bottom rule separate
        // the strip from content.
        const bool vertical = dynamic_cast<const UIContainer*>(getParent()) &&
                              dynamic_cast<const UIContainer*>(getParent())->getDirection() == EWidgetBoxLayout::Vertical;
        const Rect2D accent = vertical
                                  ? Rect2D{glm::vec2{_layoutRect.pos.x, _layoutRect.pos.y},
                                           glm::vec2{2.0f, _layoutRect.extent.y}}
                                  : Rect2D{glm::vec2{_layoutRect.pos.x, _layoutRect.pos.y},
                                           glm::vec2{_layoutRect.extent.x, 2.0f}};
        builder.addBrush(accent, FBrush::solid(style.accentColor));
    }
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (font) {
        Rect2D labelRect = _layoutRect;
        if (_bClosable) {
            labelRect.extent.x = std::max(0.0f, labelRect.extent.x - (kTabCloseSize + kTabClosePad * 2.0f));
        }
        builder.addText(labelRect, _label, style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
        if (_bClosable) {
            builder.addText(closeHitRect(), "x", style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
        }
    }
}

glm::vec2 UITabButton::computeDesiredSize() const
{
    // Theme padding drives the measure (the Layout edge is established by
    // paintSelf; resolve here is a pure read, no dependency registration
    // outside the paint walk). Absent key/theme → default-constructed
    // FTabStyle padding is the framework fallback.
    const FTabStyle& style = resolvedStyle(ReactiveBase::EDirtyLevel::Layout, false);
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    const float textWidth = font ? font->measureText(_label) : static_cast<float>(_label.size()) * 7.0f;
    const float closeW    = _bClosable ? (kTabCloseSize + kTabClosePad) : 0.0f;
    return {textWidth + style.padding.x * 2.0f + closeW,
            style.padding.y * 2.0f + (font ? font->lineHeight : 14.0f)};
}

bool UITabButton::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (keyEvent.bRepeat) {
            return true;
        }
        switch (keyEvent._keyCode) {
        case EKey::Left:
        case EKey::Up:
            if (_onNavigate) {
                _onNavigate(-1);
            }
            return true;
        case EKey::Right:
        case EKey::Down:
            if (_onNavigate) {
                _onNavigate(1);
            }
            return true;
        case EKey::Enter:
        case EKey::Space:
            if (_onActivated) {
                _onActivated();
            }
            return true;
        default:
            return false;
        }
    }

    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        const auto& mouse = static_cast<const MouseButtonPressedEvent&>(event);
        if (mouse.GetMouseButton() == EMouse::Left && _bClosable &&
            containsPoint(closeHitRect(), ctx.logicalPoint)) {
            if (_onClose) {
                _onClose();
            }
            return true;
        }
        if (mouse.GetMouseButton() == EMouse::Right) {
            if (_onContextMenu) {
                _onContextMenu(ctx.logicalPoint);
            }
            return true;
        }
    }

    if (eventType == EEvent::MouseButtonPressed && _onDragArmed) {
        const auto& mouse = static_cast<const MouseButtonPressedEvent&>(event);
        if (mouse.GetMouseButton() != EMouse::Left) {
            return false;
        }
        _bPressed    = true;
        _bDidReorder = false;
        _pressPoint  = ctx.logicalPoint;
        if (WidgetTree* tree = getTree()) {
            tree->setPointerCapture(this);
        }
        return true;
    }
    if (eventType == EEvent::MouseMoved && _bPressed && _onDragArmed &&
        getTree() && !getTree()->isDragging()) {
        auto* bar = dynamic_cast<UITabBar*>(getParent());
        if (!bar) {
            return true;
        }
        // Locked / canAccept=false: keep capture so Hybrid Drag chrome cannot
        // steal the gesture, and never arm a dock session or ghost.
        if (!bar->allowsArmedTabDrag(*this)) {
            return true;
        }
        if (glm::length(ctx.logicalPoint - _pressPoint) <= 6.0f) {
            return true;
        }
        if (bar->hitTestLayoutRect(ctx.logicalPoint)) {
            bar->reorderDraggedTab(*this, ctx.logicalPoint);
            _bDidReorder = true;
            return true;
        }
        _bPressed    = false;
        _bDidReorder = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        _onDragArmed();
        return true;
    }
    if (eventType == EEvent::MouseButtonReleased && _bPressed) {
        const bool bDidReorder = _bDidReorder;
        _bPressed    = false;
        _bDidReorder = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        if (!bDidReorder && _onActivated) {
            _onActivated();
        }
        return true;
    }

    switch (eventType) {
    case EEvent::MouseButtonPressed:
        if (_onActivated) {
            _onActivated();
        }
        return true;
    case EEvent::MouseMoved:
        _bHovered = bPointInside;
        return true;
    default:
        return false;
    }
}

UITabButton* UITabBar::addTab(const std::string& label)
{
    auto button = std::make_shared<UITabButton>(std::format("Tab_{}", label));
    button->_label     = label;
    if (!_styleKey.empty()) {
        button->_styleKey = _styleKey;
    }

    const int index = static_cast<int>(_tabs.size());
    button->_onActivated = [this, index]() { selectTab(index); };
    button->_onNavigate  = [this](int delta) { navigate(delta); };

    if (_bDraggableTabs) {
        button->_onDragArmed = [this, button = button.get()]()
        {
            const int index = indexOfTab(button);
            if (index >= 0 && index < static_cast<int>(_tabs.size()) && _onTabDragBegin) {
                _onTabDragBegin(index, _tabs[static_cast<size_t>(index)]->_label);
            }
        };
    }
    button->_onContextMenu = [this, index](const glm::vec2& logicalPoint)
    {
        if (_onTabContextMenu) {
            _onTabContextMenu(index, logicalPoint);
        }
    };

    addDetachedChild(button);
    _tabs.push_back(button.get());
    return button.get();
}

std::string UITabBar::removeTab(int index)
{
    if (index < 0 || index >= static_cast<int>(_tabs.size())) {
        return {};
    }
    UITabButton* button = _tabs[static_cast<size_t>(index)];
    const std::string label = button->_label;
    if (WidgetTree* tree = getTree()) {
        tree->detach(*button);
    }
    _tabs.erase(_tabs.begin() + index);
    rebindTabCallbacks();
    if (_selectedIndex >= static_cast<int>(_tabs.size())) {
        _selectedIndex = static_cast<int>(_tabs.size()) - 1;
    }
    else if (index < _selectedIndex) {
        --_selectedIndex;
    }
    syncSelectedTab(_selectedIndex);
    markLayoutDirty();
    return label;
}

void UITabBar::clearTabs()
{
    while (!_tabs.empty()) {
        (void)removeTab(static_cast<int>(_tabs.size()) - 1);
    }
    _selectedIndex = -1;
}

void UITabBar::selectTab(int index)
{
    syncSelectedTab(index);
    if (_selectedIndex >= 0 && _onTabSelected) {
        _onTabSelected(_selectedIndex);
    }
}

void UITabBar::syncSelectedTab(int index)
{
    if (_tabs.empty()) {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(_tabs.size()) - 1);
    if (index == _selectedIndex) {
        return;
    }
    _selectedIndex = index;
    for (int i = 0; i < static_cast<int>(_tabs.size()); ++i) {
        _tabs[static_cast<size_t>(i)]->_bSelected = (i == index);
        // _bSelected is a reflect-ed bool, not a VisualFlag: mark paint-dirty
        // manually so the tab highlight re-paints.
        _tabs[static_cast<size_t>(i)]->markPaintDirty();
    }
}

void UITabBar::navigate(int delta)
{
    if (_tabs.empty()) {
        return;
    }
    int next = _selectedIndex < 0 ? 0 : (_selectedIndex + delta) % static_cast<int>(_tabs.size());
    if (next < 0) {
        next += static_cast<int>(_tabs.size());
    }
    selectTab(next);
    if (WidgetTree* tree = getTree()) {
        tree->setFocus(_tabs[static_cast<size_t>(next)], /*bFromKeyboard=*/true);
    }
}

glm::vec2 UITabBar::computeDesiredSize() const
{
    const glm::vec2 base = UIContainer::computeDesiredSize();
    if (!_tabs.empty() || _emptyPlaceholder.empty()) {
        return base;
    }
    // Empty bar: keep a header-sized height so the zone stays a visible
    // drop target instead of collapsing to a 0-height sliver.
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 13);
    // Match the bar padding so an empty zone stays a visible drop target.
    const glm::vec2 pad = getBoxLayout().getPadding();
    const float headerH = pad.y * 2.0f + (font ? font->lineHeight : 14.0f);
    return {base.x, std::max(base.y, headerH)};
}

void UITabBar::appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const
{
    nlohmann::json tabs = nlohmann::json::array();
    for (const UITabButton* tab : _tabs) {
        if (tab) {
            tabs.push_back(tab->_label);
        }
    }
    node["control"] = {{"type", "tabBar"}, {"tabs", std::move(tabs)}, {"selected", _selectedIndex}};
}

void UITabBar::paintSelf(UIFrameBuilder& builder)
{
    // Theme resolution (style-system Phase 3): strip chrome (separator rule +
    // empty-zone placeholder) comes from FTabStyle when the key resolves.
    // Resolve unconditionally so the theme-generation edge is registered even
    // when only the rule branch paints.
    const FTabStyle& style = resolvedStyle();
    if (!_tabs.empty()) {
        // Bottom rule separating the strip from the content host below it.
        const float y = _layoutRect.pos.y + _layoutRect.extent.y - 1.0f;
        builder.addLine({_layoutRect.pos.x, y},
                        {_layoutRect.pos.x + _layoutRect.extent.x, y},
                        style.separatorColor,
                        1.0f);
    }
    if (_tabs.empty() && !_emptyPlaceholder.empty()) {
        auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 13);
        if (font) {
            builder.addText(_layoutRect, _emptyPlaceholder, style.placeholderTextColor,
                            font, EWidgetAlignH::Center, EWidgetAlignV::Center);
        }
    }
}

bool UITabBar::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    if (event.getEventType() != EEvent::MouseButtonPressed || !hitTestLayoutRect(ctx.logicalPoint)) {
        return false;
    }
    const auto& mouse = static_cast<const MouseButtonPressedEvent&>(event);
    if (mouse.GetMouseButton() == EMouse::Right) {
        if (_onTabContextMenu) {
            _onTabContextMenu(-1, ctx.logicalPoint);
        }
        return true;
    }
    if (mouse.GetMouseButton() == EMouse::Left && _onStripDoubleClick) {
        const bool bOsDouble = mouse.clickCount() >= 2;
        const uint64_t now = event.getTimestampMs();
        const bool bTimedDouble =
            _bHasLastEmptyPress && (now - _lastEmptyPressTimeMs) < kTabDoubleClickMs &&
            glm::length(ctx.logicalPoint - _lastEmptyPressPos) < kTabDoubleClickSlop;
        _lastEmptyPressTimeMs = now;
        _lastEmptyPressPos    = ctx.logicalPoint;
        _bHasLastEmptyPress   = true;
        if (bOsDouble || bTimedDouble) {
            _bHasLastEmptyPress = false;
            _onStripDoubleClick();
            return true;
        }
        return false;
    }
    return false;
}

int UITabBar::indexOfTab(const UITabButton* button) const
{
    for (int i = 0; i < tabCount(); ++i) {
        if (_tabs[static_cast<size_t>(i)] == button) {
            return i;
        }
    }
    return -1;
}

bool UITabBar::allowsArmedTabDrag(const UITabButton& button) const
{
    if (!button._bDraggable) {
        return false;
    }
    const int index = indexOfTab(&button);
    if (index < 0) {
        return false;
    }
    return !_canBeginTabDrag || _canBeginTabDrag(index);
}

void UITabBar::reorderDraggedTab(UITabButton& button, const glm::vec2& point)
{
    const int from = indexOfTab(&button);
    if (from < 0) {
        return;
    }
    int hover = -1;
    for (int i = 0; i < tabCount(); ++i) {
        if (containsPoint(_tabs[static_cast<size_t>(i)]->_layoutRect, point)) {
            hover = i;
            break;
        }
    }
    if (hover < 0 || hover == from) {
        return;
    }
    if (_onTabReordered) {
        _onTabReordered(from, hover);
    }
    moveTabVisual(from, hover);
}

void UITabBar::moveTabVisual(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= tabCount() || to >= tabCount()) {
        return;
    }
    UITabButton* button = _tabs[static_cast<size_t>(from)];
    relocateOwnedChild(*button, static_cast<size_t>(to));
    _tabs.erase(_tabs.begin() + from);
    const int dest = std::clamp(to, 0, static_cast<int>(_tabs.size()));
    _tabs.insert(_tabs.begin() + dest, button);
    rebindTabCallbacks();
    markLayoutDirty();
}

void UITabBar::rebindTabCallbacks()
{
    for (int i = 0; i < tabCount(); ++i) {
        const int index = i;
        _tabs[static_cast<size_t>(i)]->_onActivated = [this, index]() { selectTab(index); };
        _tabs[static_cast<size_t>(i)]->_onContextMenu = [this, index](const glm::vec2& logicalPoint)
        {
            if (_onTabContextMenu) {
                _onTabContextMenu(index, logicalPoint);
            }
        };
        if (_bDraggableTabs) {
            UITabButton* button = _tabs[static_cast<size_t>(i)];
            button->_onDragArmed = [this, button]()
            {
                const int current = indexOfTab(button);
                if (current >= 0 && current < tabCount() && _onTabDragBegin) {
                    _onTabDragBegin(current, _tabs[static_cast<size_t>(current)]->_label);
                }
            };
        }
    }
}

} // namespace ya
