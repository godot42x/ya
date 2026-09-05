#include "GUI/Widgets/Controls/ColorEdit.h"

#include "Core/KeyCode.h"
#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace ya
{

namespace
{

glm::vec4 hsvToRgb(float h, float s, float v)
{
    const float c = v * s;
    const float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float       r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 60.0f)       { r = c; g = x; }
    else if (h < 120.0f) { r = x; g = c; }
    else if (h < 180.0f) { g = c; b = x; }
    else if (h < 240.0f) { g = x; b = c; }
    else if (h < 300.0f) { r = x; b = c; }
    else                 { r = c; b = x; }
    return {r + m, g + m, b + m, 1.0f};
}

/// Preset color grid (the ColorEdit popup palette): paints an N-column
/// swatch grid; a press on a cell reports its color.
class FColorPalette : public UIElement
{
public:
    explicit FColorPalette(std::string name) : UIElement(std::move(name), "coloredit")
    {
        _hitFilter = EWidgetHitFilter::Stop;
        // 4 rows x 8 columns: hue ring + value steps.
        for (int row = 0; row < 4; ++row) {
            const float v = 1.0f - static_cast<float>(row) * 0.22f;
            for (int col = 0; col < 8; ++col) {
                _colors.push_back(hsvToRgb(static_cast<float>(col) * 45.0f, 0.75f, v));
            }
        }
    }

    std::function<void(const glm::vec4&)> _onPick;
    float _cellSize = 22.0f;
    int   _cols     = 8;

    void paintSelf(UIFrameBuilder& builder) override
    {
        // Nested helper, not UIStyledWidget: no sparse patch / resolved-style
        // cache. Key "coloredit" still registers theme edges via the uncached
        // helper so a theme switch repaints the palette.
        const FColorEditStyle style = resolveWidgetStyle<FColorEditStyle>(*this);
        builder.addBrush(_layoutRect, style.backgroundFill);
        for (size_t i = 0; i < _colors.size(); ++i) {
            const int col = static_cast<int>(i) % _cols;
            const int row = static_cast<int>(i) / _cols;
            builder.addSprite(Rect2D{
                                  .pos    = {_layoutRect.pos.x + static_cast<float>(col) * _cellSize,
                                             _layoutRect.pos.y + static_cast<float>(row) * _cellSize},
                                  .extent = {_cellSize, _cellSize}},
                              _colors[i], nullptr);
        }
    }

    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override
    {
        if (event.getEventType() == EEvent::MouseButtonPressed &&
            hitTestLayoutRect(ctx.logicalPoint)) {
            const int col = static_cast<int>((ctx.logicalPoint.x - _layoutRect.pos.x) / _cellSize);
            const int row = static_cast<int>((ctx.logicalPoint.y - _layoutRect.pos.y) / _cellSize);
            const int index = row * _cols + col;
            if (index >= 0 && index < static_cast<int>(_colors.size()) && _onPick) {
                _onPick(_colors[index]);
            }
            return true;
        }
        return false;
    }

private:
    std::vector<glm::vec4> _colors;
};

} // namespace

Rect2D UIColorEdit::swatchRect() const
{
    return Rect2D{
        .pos    = {_layoutRect.pos.x + 4.0f, _layoutRect.pos.y + 4.0f},
        .extent = {_swatchSize, _layoutRect.extent.y - 8.0f},
    };
}

void UIColorEdit::setColor(const glm::vec4& value, bool bNotify)
{
    if (_color == value && !_bMixed) {
        return;
    }
    _color  = value;
    _bMixed = false;
    invalidateProperty(EUIPropertyImpact::Paint);
    if (bNotify && _onColorChanged) {
        _onColorChanged(_color);
    }
}

void UIColorEdit::setMixed(bool mixed)
{
    if (_bMixed == mixed) {
        return;
    }
    _bMixed = mixed;
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UIColorEdit::adjustActiveChannel(float delta)
{
    glm::vec4 next = _color;
    next[_activeChannel] = std::clamp(next[_activeChannel] + delta, 0.0f, 1.0f);
    setColor(next);
}

void UIColorEdit::openPalette()
{
    closePalette();
    auto overlay = std::make_shared<UIPopupOverlay>("ColorPaletteOverlay");
    overlay->setStyleKey("popup");
    overlay->_bModal     = false; // transparent shield: click outside closes
    overlay->_contentPos = {swatchRect().pos.x, swatchRect().pos.y + swatchRect().extent.y + 4.0f};

    auto palette = std::make_shared<FColorPalette>("ColorPaletteGrid");
    overlay->_contentExtent = {palette->_cellSize * static_cast<float>(palette->_cols),
                               palette->_cellSize * 4.0f};
    palette->_onPick = [this, overlay](const glm::vec4& picked)
    {
        setColor(picked);
        overlay->close();
    };
    overlay->addDetachedChild(palette);

    overlay->_onDismiss = [this]() { _paletteOverlay.reset(); };
    _paletteOverlay = overlay;
    if (WidgetTree* tree = getTree()) {
        overlay->open(*tree);
    }
}

void UIColorEdit::closePalette()
{
    if (_paletteOverlay) {
        const auto overlay = _paletteOverlay;
        _paletteOverlay.reset();
        overlay->close();
    }
}

void UIColorEdit::paintSelf(UIFrameBuilder& builder)
{
    const FColorEditStyle& style = resolvedStyle();
    builder.addBrush(_layoutRect, style.backgroundFill);
    const glm::vec4 swatchColor = _bMixed ? glm::vec4(0.45f, 0.45f, 0.45f, 1.0f) : _color;
    builder.addSprite(swatchRect(), swatchColor, nullptr);

    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 11);
    if (_bMixed && font) {
        builder.addText(swatchRect(), "—", style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
        return;
    }

    // Channel strip: four cells, the active one highlighted.
    const float stripX = swatchRect().pos.x + _swatchSize + 8.0f;
    const float cellW  = 26.0f;
    const float cellH  = 12.0f;
    const float cellY  = _layoutRect.pos.y + (_layoutRect.extent.y - cellH) * 0.5f;
    static const char* kNames[4] = {"R", "G", "B", "A"};
    for (int ch = 0; ch < 4; ++ch) {
        const Rect2D cell{
            .pos    = {stripX + static_cast<float>(ch) * (cellW + 2.0f), cellY},
            .extent = {cellW, cellH},
        };
        if (ch == _activeChannel) {
            builder.addSprite(cell, style.channelHighlight, nullptr);
        }
        else {
            builder.addSprite(cell, _color * glm::vec4(0.4f, 0.4f, 0.4f, 1.0f), nullptr);
        }
        if (font) {
            builder.addText(cell, kNames[ch], style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
        }
    }
}

bool UIColorEdit::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Left) {
            adjustActiveChannel(-0.05f);
            return true;
        }
        if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Right) {
            adjustActiveChannel(0.05f);
            return true;
        }
        return false;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        // Channel strip click: select the active channel (no drag).
        const float stripX = swatchRect().pos.x + _swatchSize + 8.0f;
        const float cellW  = 26.0f;
        const float cellH  = 12.0f;
        const float cellY  = _layoutRect.pos.y + (_layoutRect.extent.y - cellH) * 0.5f;
        for (int ch = 0; ch < 4; ++ch) {
            const Rect2D cell{
                .pos    = {stripX + static_cast<float>(ch) * (cellW + 2.0f), cellY},
                .extent = {cellW, cellH},
            };
            const bool bInside = ctx.logicalPoint.x >= cell.pos.x &&
                                 ctx.logicalPoint.x <= cell.pos.x + cell.extent.x &&
                                 ctx.logicalPoint.y >= cell.pos.y &&
                                 ctx.logicalPoint.y <= cell.pos.y + cell.extent.y;
            if (bInside) {
                if (_activeChannel != ch) {
                    _activeChannel = ch;
                    markPaintDirty();
                }
                return true;
            }
        }
        // Swatch click: open the preset palette popup.
        const Rect2D swatch = swatchRect();
        const bool bOnSwatch = ctx.logicalPoint.x >= swatch.pos.x &&
                               ctx.logicalPoint.x <= swatch.pos.x + swatch.extent.x &&
                               ctx.logicalPoint.y >= swatch.pos.y &&
                               ctx.logicalPoint.y <= swatch.pos.y + swatch.extent.y;
        if (bOnSwatch) {
            openPalette();
            return true;
        }
        // Anywhere else in the control: drag adjusts the active channel.
        if (hitTestLayoutRect(ctx.logicalPoint)) {
            _bDragging = true;
            _dragStart = ctx.logicalPoint;
            if (WidgetTree* tree = getTree()) {
                tree->setPointerCapture(this);
            }
            return true;
        }
        return false;
    }

    if (eventType == EEvent::MouseMoved) {
        if (_bDragging) {
            adjustActiveChannel((ctx.logicalPoint.x - _dragStart.x) * 0.01f);
            _dragStart = ctx.logicalPoint;
        }
        return true;
    }

    if (eventType == EEvent::MouseButtonReleased) {
        if (_bDragging) {
            _bDragging = false;
            if (WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
        }
        return true;
    }

    return false;
}

} // namespace ya
