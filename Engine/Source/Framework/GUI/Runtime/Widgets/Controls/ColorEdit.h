#pragma once

#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>

namespace ya
{

/// Color edit (minimal): a color swatch; clicking the swatch cycles the
/// active channel (R/G/B/A), dragging adjusts it. The 4 channels are shown
/// as a compact strip under the swatch. Full picker is a later step.
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

    glm::vec4   _color          = {1.0f, 1.0f, 1.0f, 1.0f};
    int         _activeChannel  = 0; // 0=R 1=G 2=B 3=A
    uint32_t    _fontSize       = 13;
    float       _swatchSize     = 18.0f;

    std::function<void(const glm::vec4& color)> _onColorChanged;

    void setColor(const glm::vec4& value, bool bNotify = true);
    void setMixed(bool mixed);
    [[nodiscard]] bool isMixed() const { return _bMixed; }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "colorEdit"},
                               {"color", {_color.r, _color.g, _color.b, _color.a}},
                               {"mixed", _bMixed},
                               {"activeChannel", _activeChannel}};
    }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void clearTransientInputState() override
    {
        _bDragging = false;
        closePalette();
    }

  private:
    [[nodiscard]] Rect2D swatchRect() const;
    void adjustActiveChannel(float delta);
    void openPalette();
    void closePalette();
    VisualFlag _bDragging{*this};
    glm::vec2  _dragStart{0.0f, 0.0f};
    bool       _bMixed = false;
    std::shared_ptr<struct UIPopupOverlay> _paletteOverlay;
};

} // namespace ya
