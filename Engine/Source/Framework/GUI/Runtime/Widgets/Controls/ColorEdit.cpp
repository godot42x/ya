#include "GUI/Widgets/Controls/ColorEdit.h"

#include "Core/KeyCode.h"
#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/TextEdit.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <optional>
#include <string>

namespace ya
{

namespace
{

constexpr float kSvSize     = 160.0f;
constexpr float kHueBarH    = 14.0f;
constexpr float kPickerPad  = 8.0f;
constexpr float kHexRowH    = 22.0f;
constexpr int   kHueSegments = 6;

bool contains(const Rect2D& rect, const glm::vec2& point)
{
    return point.x >= rect.pos.x && point.x < rect.pos.x + rect.extent.x &&
           point.y >= rect.pos.y && point.y < rect.pos.y + rect.extent.y;
}

glm::vec4 hsvToRgb(float h, float s, float v, float a)
{
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) {
        h += 360.0f;
    }
    const float c = v * s;
    const float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float       r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 60.0f) {
        r = c;
        g = x;
    }
    else if (h < 120.0f) {
        r = x;
        g = c;
    }
    else if (h < 180.0f) {
        g = c;
        b = x;
    }
    else if (h < 240.0f) {
        g = x;
        b = c;
    }
    else if (h < 300.0f) {
        r = x;
        b = c;
    }
    else {
        r = c;
        b = x;
    }
    return {r + m, g + m, b + m, a};
}

void rgbToHsv(const glm::vec3& rgb, float& h, float& s, float& v)
{
    const float r    = std::clamp(rgb.r, 0.0f, 1.0f);
    const float g    = std::clamp(rgb.g, 0.0f, 1.0f);
    const float b    = std::clamp(rgb.b, 0.0f, 1.0f);
    const float maxC = std::max(r, std::max(g, b));
    const float minC = std::min(r, std::min(g, b));
    const float d    = maxC - minC;
    v                = maxC;
    s                = maxC <= 1e-6f ? 0.0f : d / maxC;
    if (d <= 1e-6f) {
        return;
    }
    if (maxC == r) {
        h = 60.0f * std::fmod((g - b) / d, 6.0f);
    }
    else if (maxC == g) {
        h = 60.0f * ((b - r) / d + 2.0f);
    }
    else {
        h = 60.0f * ((r - g) / d + 4.0f);
    }
    if (h < 0.0f) {
        h += 360.0f;
    }
}

std::string formatHex(const glm::vec4& color)
{
    const auto byte = [](float ch) {
        return static_cast<int>(std::clamp(std::round(ch * 255.0f), 0.0f, 255.0f));
    };
    return std::format("#{:02X}{:02X}{:02X}{:02X}", byte(color.r), byte(color.g), byte(color.b), byte(color.a));
}

std::optional<glm::vec4> parseHex(std::string_view text)
{
    std::string hex;
    hex.reserve(text.size());
    for (const char ch : text) {
        if (ch == '#' || ch == ' ') {
            continue;
        }
        hex.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    if (hex.size() != 6 && hex.size() != 8) {
        return std::nullopt;
    }
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'A' && c <= 'F') {
            return 10 + (c - 'A');
        }
        return -1;
    };
    const auto channel = [&](size_t i) -> std::optional<float> {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        return static_cast<float>((hi << 4) | lo) / 255.0f;
    };
    const auto r = channel(0);
    const auto g = channel(2);
    const auto b = channel(4);
    if (!r || !g || !b) {
        return std::nullopt;
    }
    float a = 1.0f;
    if (hex.size() == 8) {
        const auto alpha = channel(6);
        if (!alpha) {
            return std::nullopt;
        }
        a = *alpha;
    }
    return glm::vec4{*r, *g, *b, a};
}

/// SV square + hue bar + hex/rgba readout. Not a UICompoundWidget: the picker
/// is one paint/input surface hosted in the ColorEdit popup.
class FColorPicker : public UIElement
{
public:
    explicit FColorPicker(std::string name) : UIElement(std::move(name), "coloredit")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    std::function<void(const glm::vec4&)> _onColorChanged;
    float _h = 0.0f;
    float _s = 0.0f;
    float _v = 1.0f;
    float _a = 1.0f;

    void setFromRgba(const glm::vec4& color)
    {
        rgbToHsv(glm::vec3(color), _h, _s, _v);
        _a = color.a;
        if (!_bEditingHex) {
            _hexBuffer = formatHex(currentColor());
            _hexEdit.selectAll(_hexBuffer.size());
        }
        invalidateProperty(EUIPropertyImpact::Paint);
    }

    [[nodiscard]] glm::vec4 currentColor() const { return hsvToRgb(_h, _s, _v, _a); }

    [[nodiscard]] glm::vec2 computeDesiredSize() const override { return computeIntrinsicSize(); }
    [[nodiscard]] glm::vec2 computeIntrinsicSize() const override
    {
        return {kPickerPad * 2.0f + kSvSize, kPickerPad * 3.0f + kSvSize + kHueBarH + kHexRowH};
    }

    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override
    {
        const glm::vec4 color = currentColor();
        node["control"]       = {{"type", "colorPicker"},
                                 {"h", _h},
                                 {"s", _s},
                                 {"v", _v},
                                 {"hex", _hexBuffer},
                                 {"color", {color.r, color.g, color.b, color.a}}};
    }

    void paintSelf(UIFrameBuilder& builder) override
    {
        const FColorEditStyle style = resolveWidgetStyle<FColorEditStyle>(*this);
        builder.addBrush(_layoutRect, style.backgroundFill);
        const Rect2D sv  = svRect();
        const Rect2D hue = hueRect();
        // GPU quads are two triangles: a 2D four-corner field (white/hue/black/black)
        // interpolates a gray diagonal. ImGui paints two 1D layers instead:
        // S = white→hue across, V = transparent→black down, alpha-blended.
        const glm::vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
        const glm::vec4 black{0.0f, 0.0f, 0.0f, 1.0f};
        const glm::vec4 clear{0.0f, 0.0f, 0.0f, 0.0f};
        const glm::vec4 hueColor = hsvToRgb(_h, 1.0f, 1.0f, 1.0f);
        builder.addRectFilledMultiColor(sv, white, hueColor, hueColor, white);
        builder.addRectFilledMultiColor(sv, clear, clear, black, black);
        const glm::vec2 cursor{sv.pos.x + _s * sv.extent.x, sv.pos.y + (1.0f - _v) * sv.extent.y};
        builder.addRectOutline(Rect2D{.pos = cursor - glm::vec2(4.0f), .extent = {8.0f, 8.0f}},
                               {1.0f, 1.0f, 1.0f, 1.0f},
                               1.0f);

        const float hueSegW = hue.extent.x / static_cast<float>(kHueSegments);
        for (int i = 0; i < kHueSegments; ++i) {
            const glm::vec4 left  = hsvToRgb(static_cast<float>(i) * 60.0f, 1.0f, 1.0f, 1.0f);
            const glm::vec4 right = hsvToRgb(static_cast<float>(i + 1) * 60.0f, 1.0f, 1.0f, 1.0f);
            builder.addRectFilledMultiColor(
                Rect2D{.pos    = {hue.pos.x + static_cast<float>(i) * hueSegW, hue.pos.y},
                       .extent = {hueSegW, hue.extent.y}},
                left,
                right,
                right,
                left);
        }
        const float hueX = hue.pos.x + (_h / 360.0f) * hue.extent.x;
        builder.addRectOutline(Rect2D{.pos = {hueX - 2.0f, hue.pos.y}, .extent = {4.0f, hue.extent.y}},
                               {1.0f, 1.0f, 1.0f, 1.0f},
                               1.0f);

        auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 12);
        const Rect2D hex = hexRect();
        builder.addSprite(hex, currentColor(), nullptr);
        if (font) {
            if (_bEditingHex) {
                textEditPaint(builder,
                              hex,
                              _hexBuffer,
                              _hexEdit,
                              font,
                              style.textColor,
                              style.textColor,
                              kTextEditSelectionColor,
                              EWidgetAlignH::Left,
                              0.0f,
                              true);
            }
            else {
                builder.addText(hex, _hexBuffer, style.textColor, font, EWidgetAlignH::Left, EWidgetAlignV::Center);
            }
        }
    }

    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override
    {
        const EEvent::T eventType = event.getEventType();
        if (_bEditingHex) {
            if (eventType == EEvent::KeyTyped) {
                textEditInsert(_hexBuffer, _hexEdit, static_cast<const KeyTypedEvent&>(event).getText(), 9);
                invalidateProperty(EUIPropertyImpact::Paint);
                return true;
            }
            if (eventType == EEvent::KeyPressed) {
                const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
                if (keyEvent._keyCode == EKey::Enter) {
                    commitHex();
                    return true;
                }
                if (keyEvent._keyCode == EKey::Escape) {
                    _bEditingHex = false;
                    _hexBuffer   = formatHex(currentColor());
                    invalidateProperty(EUIPropertyImpact::Paint);
                    return true;
                }
                if (textEditHandleKey(_hexBuffer, _hexEdit, keyEvent, getTree(), 9)) {
                    invalidateProperty(EUIPropertyImpact::Paint);
                    return true;
                }
            }
        }

        if (eventType == EEvent::MouseButtonPressed) {
            const auto& mouse = static_cast<const MouseButtonPressedEvent&>(event);
            if (mouse.GetMouseButton() != EMouse::Left) {
                return false;
            }
            if (WidgetTree* tree = getTree()) {
                tree->setFocus(this);
                tree->setPointerCapture(this);
            }
            if (contains(hexRect(), ctx.logicalPoint)) {
                _bEditingHex = true;
                _hexBuffer   = formatHex(currentColor());
                _hexEdit.selectAll(_hexBuffer.size());
                _drag = EDrag::None;
                invalidateProperty(EUIPropertyImpact::Paint);
                return true;
            }
            _bEditingHex = false;
            if (contains(svRect(), ctx.logicalPoint)) {
                _drag = EDrag::Sv;
                applySv(ctx.logicalPoint);
                return true;
            }
            if (contains(hueRect(), ctx.logicalPoint)) {
                _drag = EDrag::Hue;
                applyHue(ctx.logicalPoint);
                return true;
            }
            _drag = EDrag::None;
            return true;
        }
        if (eventType == EEvent::MouseMoved && ctx.bViaCapture) {
            if (_drag == EDrag::Sv) {
                applySv(ctx.logicalPoint);
            }
            else if (_drag == EDrag::Hue) {
                applyHue(ctx.logicalPoint);
            }
            return true;
        }
        if (eventType == EEvent::MouseButtonReleased) {
            _drag = EDrag::None;
            if (WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
            return true;
        }
        return false;
    }

    void clearTransientInputState() override
    {
        _drag        = EDrag::None;
        _bEditingHex = false;
    }

private:
    enum class EDrag : uint8_t
    {
        None,
        Sv,
        Hue,
    };

    [[nodiscard]] Rect2D svRect() const
    {
        return Rect2D{.pos    = {_layoutRect.pos.x + kPickerPad, _layoutRect.pos.y + kPickerPad},
                      .extent = {kSvSize, kSvSize}};
    }
    [[nodiscard]] Rect2D hueRect() const
    {
        return Rect2D{.pos    = {_layoutRect.pos.x + kPickerPad,
                                 _layoutRect.pos.y + kPickerPad * 2.0f + kSvSize},
                      .extent = {kSvSize, kHueBarH}};
    }
    [[nodiscard]] Rect2D hexRect() const
    {
        return Rect2D{.pos    = {_layoutRect.pos.x + kPickerPad,
                                 _layoutRect.pos.y + kPickerPad * 3.0f + kSvSize + kHueBarH},
                      .extent = {kSvSize, kHexRowH}};
    }

    void emitColor()
    {
        const glm::vec4 color = currentColor();
        if (!_bEditingHex) {
            _hexBuffer = formatHex(color);
        }
        invalidateProperty(EUIPropertyImpact::Paint);
        if (_onColorChanged) {
            _onColorChanged(color);
        }
    }

    void applySv(const glm::vec2& point)
    {
        const Rect2D sv = svRect();
        _s = std::clamp((point.x - sv.pos.x) / std::max(1.0f, sv.extent.x), 0.0f, 1.0f);
        _v = std::clamp(1.0f - (point.y - sv.pos.y) / std::max(1.0f, sv.extent.y), 0.0f, 1.0f);
        emitColor();
    }

    void applyHue(const glm::vec2& point)
    {
        const Rect2D hue = hueRect();
        _h = std::clamp((point.x - hue.pos.x) / std::max(1.0f, hue.extent.x), 0.0f, 1.0f) * 360.0f;
        emitColor();
    }

    void commitHex()
    {
        if (const auto parsed = parseHex(_hexBuffer)) {
            setFromRgba(*parsed);
            if (_onColorChanged) {
                _onColorChanged(*parsed);
            }
        }
        else {
            _hexBuffer = formatHex(currentColor());
        }
        _bEditingHex = false;
        invalidateProperty(EUIPropertyImpact::Paint);
    }

    EDrag          _drag = EDrag::None;
    bool           _bEditingHex = false;
    std::string    _hexBuffer   = "#FFFFFFFF";
    FTextEditState _hexEdit;
};

} // namespace

Rect2D UIColorEdit::swatchRect() const
{
    const glm::vec2 pad = resolvedStyle().padding;
    return Rect2D{
        .pos    = {_layoutRect.pos.x + pad.x, _layoutRect.pos.y + pad.y},
        .extent = {_swatchSize, std::max(8.0f, _layoutRect.extent.y - pad.y * 2.0f)},
    };
}

Rect2D UIColorEdit::channelRect(int channel) const
{
    const glm::vec2 pad    = resolvedStyle().padding;
    const Rect2D     swatch = swatchRect();
    const float      x0    = swatch.pos.x + swatch.extent.x + pad.x;
    const float      avail  = std::max(32.0f, _layoutRect.pos.x + _layoutRect.extent.x - pad.x - x0);
    const float      cellW  = avail / 4.0f;
    return Rect2D{
        .pos    = {x0 + static_cast<float>(channel) * cellW, _layoutRect.pos.y + pad.y},
        .extent = {std::max(8.0f, cellW - pad.x), std::max(8.0f, _layoutRect.extent.y - pad.y * 2.0f)},
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

void UIColorEdit::adjustChannel(int channel, float delta)
{
    if (channel < 0 || channel > 3) {
        return;
    }
    glm::vec4 next = _color;
    next[channel] = std::clamp(next[channel] + delta, 0.0f, 1.0f);
    setColor(next);
}

void UIColorEdit::openPalette()
{
    closePalette();
    auto overlay = std::make_shared<UIPopupOverlay>("ColorPickerOverlay");
    overlay->setStyleKey("popup");
    overlay->_bModal = false;

    auto picker = std::make_shared<FColorPicker>("ColorPicker");
    picker->setFromRgba(_color);
    picker->_onColorChanged = [this](const glm::vec4& picked) { setColor(picked); };
    const glm::vec2 pickerSize = picker->computeDesiredSize();
    const Rect2D    swatch     = swatchRect();
    glm::vec2       pos        = {swatch.pos.x, swatch.pos.y + swatch.extent.y + 4.0f};
    if (WidgetTree* tree = getTree()) {
        const float viewH = static_cast<float>(tree->getLogicalExtent().height);
        if (pos.y + pickerSize.y > viewH && swatch.pos.y - 4.0f - pickerSize.y >= 0.0f) {
            pos.y = swatch.pos.y - 4.0f - pickerSize.y;
        }
    }
    overlay->_contentPos    = pos;
    overlay->_contentExtent = pickerSize;
    overlay->addDetachedChild(picker);

    overlay->_onDismiss = [this]() { _paletteOverlay.reset(); };
    _paletteOverlay     = overlay;
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
    builder.addRectOutline(insetRect(swatchRect(), 1.0f), style.textColor * glm::vec4(1.0f, 1.0f, 1.0f, 0.35f), 1.0f);

    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, style.fontSize);
    if (_bMixed && font) {
        builder.addText(swatchRect(), "—", style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
        return;
    }

    static const char* kNames[4] = {"R", "G", "B", "A"};
    const glm::vec2 innerPad{std::max(4.0f, style.padding.x * 0.5f), 0.0f};
    for (int ch = 0; ch < 4; ++ch) {
        const Rect2D cell = channelRect(ch);
        builder.addSprite(cell, glm::vec4(0.16f, 0.18f, 0.22f, 1.0f), nullptr);
        builder.addRectOutline(insetRect(cell, 1.0f), style.textColor * glm::vec4(1.0f, 1.0f, 1.0f, 0.22f), 1.0f);
        if (!font) {
            continue;
        }
        Rect2D inner = cell;
        inner.pos += innerPad;
        inner.extent = glm::max(inner.extent - innerPad * 2.0f, glm::vec2(0.0f));
        const float prefixW = font->measureText(kNames[ch]) + innerPad.x;
        Rect2D prefix = inner;
        prefix.extent.x = std::min(prefixW, inner.extent.x);
        Rect2D value = inner;
        value.pos.x += prefix.extent.x;
        value.extent.x = std::max(0.0f, inner.extent.x - prefix.extent.x);
        builder.pushClip(cell);
        builder.addText(prefix, kNames[ch], style.textColor, font, EWidgetAlignH::Left, EWidgetAlignV::Center);
        builder.addText(value,
                        std::format("{:.2f}", _color[ch]),
                        style.textColor,
                        font,
                        EWidgetAlignH::Left,
                        EWidgetAlignV::Center);
        builder.popClip();
    }
}

bool UIColorEdit::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        if (_dragChannel < 0) {
            return false;
        }
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Left) {
            adjustChannel(_dragChannel, -0.05f);
            return true;
        }
        if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Right) {
            adjustChannel(_dragChannel, 0.05f);
            return true;
        }
        return false;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        if (contains(swatchRect(), ctx.logicalPoint)) {
            if (isPickerOpen()) {
                closePalette();
            }
            else {
                openPalette();
            }
            return true;
        }
        for (int ch = 0; ch < 4; ++ch) {
            if (contains(channelRect(ch), ctx.logicalPoint)) {
                _dragChannel = ch;
                _bDragging   = true;
                _dragStart   = ctx.logicalPoint;
                if (WidgetTree* tree = getTree()) {
                    tree->setPointerCapture(this);
                }
                return true;
            }
        }
        return hitTestLayoutRect(ctx.logicalPoint);
    }

    if (eventType == EEvent::MouseMoved) {
        if (_bDragging && _dragChannel >= 0) {
            adjustChannel(_dragChannel, (ctx.logicalPoint.x - _dragStart.x) * 0.01f);
            _dragStart = ctx.logicalPoint;
        }
        return true;
    }

    if (eventType == EEvent::MouseButtonReleased) {
        if (_bDragging) {
            _bDragging   = false;
            _dragChannel = -1;
            if (WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
        }
        return true;
    }

    return false;
}

} // namespace ya
