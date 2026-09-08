#pragma once

#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>

namespace ya
{

/// Color edit: left swatch opens an SV/hue/hex picker; four RGBA drag
/// fields sit to the right (ImGui ColorEdit analog). Channel fields only
/// edit their own component — there is no selected/active channel. Isolate
/// a channel by setting the others to 0, or put multi-select masks on the
/// app (e.g. RenderTargetView). The picker SV square is ImGui's two 1D
/// vertex-color quads (S then V-via-alpha), not one 2D quad and not a cell grid.
struct YA_GUI_API UIColorEdit : public UIElement, public UIStyledWidget<UIColorEdit, FColorEditStyle>
{
    YA_REFLECT_BEGIN(UIColorEdit, UIElement)
    YA_REFLECT_FIELD(_color, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FColorEditStyle)

    explicit UIColorEdit(std::string name = "ColorEdit") : UIElement(std::move(name), "coloredit")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIColorEdit>; }

    glm::vec4 _color         = {1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t  _fontSize      = 13;
    float     _swatchSize    = 22.0f;

    std::function<void(const glm::vec4& color)> _onColorChanged;

    void setColor(const glm::vec4& value, bool bNotify = true);
    void setMixed(bool mixed);
    [[nodiscard]] bool isMixed() const { return _bMixed; }
    [[nodiscard]] bool isPickerOpen() const { return static_cast<bool>(_paletteOverlay); }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "colorEdit"},
                           {"color", {_color.r, _color.g, _color.b, _color.a}},
                           {"mixed", _bMixed},
                           {"pickerOpen", isPickerOpen()}};
    }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void clearTransientInputState() override
    {
        _bDragging   = false;
        _dragChannel = -1;
        closePalette();
    }

  private:
    [[nodiscard]] Rect2D swatchRect() const;
    [[nodiscard]] Rect2D channelRect(int channel) const;
    void adjustChannel(int channel, float delta);
    void openPalette();
    void closePalette();
    VisualFlag _bDragging{*this};
    int        _dragChannel = -1;
    glm::vec2  _dragStart{0.0f, 0.0f};
    bool       _bMixed = false;
    std::shared_ptr<struct UIPopupOverlay> _paletteOverlay;
};

} // namespace ya
