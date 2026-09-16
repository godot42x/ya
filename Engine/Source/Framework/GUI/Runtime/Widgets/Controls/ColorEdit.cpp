#include "GUI/Widgets/Controls/ColorEdit.h"

#include "Core/KeyCode.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DragFloat.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/TextEdit.h"
#include "GUI/Widgets/Style.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Render/Resources/FontManager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ya
{

namespace
{

constexpr float kSvSize     = 160.0f;
constexpr float kHueBarH    = 14.0f;
constexpr float kPickerPad  = 8.0f;
constexpr float kHexRowH    = 22.0f;
constexpr float kChannelRowH = 22.0f;
constexpr float kMinChannelCell = 56.0f;
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

    [[nodiscard]] bool wantsTextInput() const override { return _bEditingHex; }

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

class FColorSwatch final : public UIElement
{
  public:
    explicit FColorSwatch(std::string name) : UIElement(std::move(name), "coloredit")
    {
        _hitFilter = EWidgetHitFilter::Stop;
    }

    glm::vec4              color{1.0f, 1.0f, 1.0f, 1.0f};
    bool                   bMixed = false;
    std::function<void()>  onClick;

    void setDisplay(const glm::vec4& value, bool mixed)
    {
        color  = value;
        bMixed = mixed;
        invalidateProperty(EUIPropertyImpact::Paint);
    }

    bool isHoverable() const override { return true; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override { _bHovered = false; }
    void resetHoverState() override { _bHovered = false; }

    void paintSelf(UIFrameBuilder& builder) override
    {
        const FColorEditStyle& style = resolveWidgetStyle<FColorEditStyle>(*this);
        const glm::vec4 fill = bMixed ? glm::vec4(0.45f, 0.45f, 0.45f, 1.0f) : color;
        // One surface value = edge ring + fill inset inside it, the same path a
        // button or field takes. The edge comes from the STYLE, and it is the
        // strong end of the ramp: the swatch sits on the panel, so its boundary
        // has to be findable even when the picked color matches the panel.
        builder.addRoundedSurface(_layoutRect,
                                  fill,
                                  _bHovered ? style.swatchHoverBorderColor : style.swatchBorderColor,
                                  style.swatchCornerRadius,
                                  style.swatchBorderThickness);
        if (!bMixed) {
            return;
        }
        auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, style.fontSize);
        if (font) {
            builder.addText(_layoutRect, "—", style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
        }
    }

    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override
    {
        if (event.getEventType() != EEvent::MouseButtonPressed) {
            return hitTestLayoutRect(ctx.logicalPoint);
        }
        if (onClick) {
            onClick();
        }
        return true;
    }

  private:
    VisualFlag _bHovered{*this};
};

} // namespace

int UIColorEdit::visibleChannelCount() const
{
    return std::clamp(_channelCount, 1, 4);
}

std::string UIColorEdit::channelStyleKey() const
{
    constexpr std::string_view kSuffix = "coloredit";
    if (_styleKey.size() > kSuffix.size() && _styleKey.ends_with(kSuffix) &&
        _styleKey[_styleKey.size() - kSuffix.size() - 1] == '.') {
        std::string key = _styleKey.substr(0, _styleKey.size() - kSuffix.size());
        key += StyleKey::DragFloat;
        return key;
    }
    return std::string(StyleKey::DragFloat);
}

void UIColorEdit::syncChannelVisibility()
{
    const int visible = visibleChannelCount();
    for (int i = 0; i < 4; ++i) {
        if (!_channels[static_cast<size_t>(i)]) {
            continue;
        }
        _channels[static_cast<size_t>(i)]->setVisibility(
            i < visible ? EWidgetVisibility::Visible : EWidgetVisibility::Collapsed);
    }
}

void UIColorEdit::syncChannelsFromColor()
{
    _bSyncing = true;
    for (int i = 0; i < 4; ++i) {
        if (auto& channel = _channels[static_cast<size_t>(i)]) {
            channel->setValue(_color[i], false);
            channel->setMixed(_bMixed);
        }
    }
    if (auto* swatch = dynamic_cast<FColorSwatch*>(_swatch.get())) {
        swatch->setDisplay(_color, _bMixed);
    }
    _bSyncing = false;
}

glm::vec2 UIColorEdit::computeIntrinsicSize() const
{
    const glm::vec2 pad   = resolvedStyle().padding;
    const int       count = visibleChannelCount();
    return {
        pad.x * 2.0f + _swatchSize + 4.0f + kMinChannelCell * static_cast<float>(count),
        pad.y * 2.0f + kChannelRowH,
    };
}

void UIColorEdit::applyHostPadding()
{
    if (getChildren().empty()) {
        return;
    }
    if (auto* row = dynamic_cast<UIContainer*>(getChildren().front().get())) {
        row->setPadding(resolvedStyle().padding);
    }
}

void UIColorEdit::onAttached()
{
    applyHostPadding();
}

void UIColorEdit::construct()
{
    static const char* kPrefix[4] = {"R", "G", "B", "A"};
    auto swatch = std::make_shared<FColorSwatch>("Swatch");
    swatch->setStyleKey(_styleKey);
    swatch->onClick = [this]()
    {
        if (isPickerOpen()) {
            closePalette();
        }
        else {
            openPalette();
        }
    };
    _swatch = swatch;

    auto row = ui::row("ColorEditRow").setSpacing(4.0f).setPadding(resolvedStyle().padding);
    row.child(_swatch, ui::boxSlot().preferredSize({_swatchSize, kChannelRowH}));
    for (int i = 0; i < 4; ++i) {
        auto drag = std::make_shared<UIDragFloat>(std::string("Channel") + kPrefix[i]);
        drag->setPrefix(kPrefix[i]);
        drag->_min      = 0.0f;
        drag->_max      = 1.0f;
        drag->_decimals = 2;
        drag->setStyleKey(channelStyleKey());
        drag->_onValueChanged = [this, i](float value)
        {
            if (_bSyncing) {
                return;
            }
            glm::vec4 next = _color;
            next[i] = value;
            setColor(next);
        };
        _channels[static_cast<size_t>(i)] = drag;
        row.child(drag, ui::boxSlot().fillWidth().preferredSize({kMinChannelCell, kChannelRowH}));
    }
    addDetachedChild(row.release());
    syncChannelVisibility();
    syncChannelsFromColor();
}

void UIColorEdit::setChannelCount(int count)
{
    count = std::clamp(count, 1, 4);
    if (_channelCount == count) {
        return;
    }
    _channelCount = count;
    syncChannelVisibility();
    invalidateProperty(EUIPropertyImpact::Layout);
}

void UIColorEdit::setColor(const glm::vec4& value, bool bNotify)
{
    if (_color == value && !_bMixed) {
        return;
    }
    _color  = value;
    _bMixed = false;
    syncChannelsFromColor();
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
    syncChannelsFromColor();
    invalidateProperty(EUIPropertyImpact::Paint);
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
    const Rect2D    swatch     = _swatch ? _swatch->getLayoutRect() : _layoutRect;
    glm::vec2 pos = {swatch.pos.x, swatch.pos.y + swatch.extent.y + 4.0f};
    if (WidgetTree* tree = getTree()) {
        pos = UIPopupOverlay::fitContentPos(*tree, pos, pickerSize, &swatch);
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

} // namespace ya
