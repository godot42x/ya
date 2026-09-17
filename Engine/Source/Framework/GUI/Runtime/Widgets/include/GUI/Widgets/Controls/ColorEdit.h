#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Widgets/Theme.h"

#include <array>
#include <functional>
#include <memory>

namespace ya
{

struct UIDragFloat;
struct UIPopupOverlay;

/// Color edit: a compound of a swatch + per-channel `UIDragFloat` (ImGui
/// ColorEdit analog). Channel fields only edit their own component — there
/// is no selected/active channel. The popup SV square is ImGui's two 1D
/// vertex-color quads (S then V-via-alpha), not one 2D quad and not a cell
/// grid; that picker stays a dedicated paint surface, not more widgets.
struct YA_GUI_API UIColorEdit : public UICompoundWidget, public UIStyledWidget<UIColorEdit, FColorEditStyle>
{
    YA_REFLECT_BEGIN(UIColorEdit, UIElement)
    YA_REFLECT_FIELD(_color, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FColorEditStyle)

    explicit UIColorEdit(std::string name = "ColorEdit") : UICompoundWidget(std::move(name), "coloredit") {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIColorEdit>; }

    glm::vec4 _color         = {1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t  _fontSize      = 13;
    float     _swatchSize    = 18.0f;
    /// 3 = RGB, 4 = RGBA. Extra channels stay in `_color` but are not shown.
    int       _channelCount  = 4;

    std::function<void(const glm::vec4& color)> _onColorChanged;

    void setColor(const glm::vec4& value, bool bNotify = true);
    void setChannelCount(int count);
    void setMixed(bool mixed);
    [[nodiscard]] bool isMixed() const { return _bMixed; }
    [[nodiscard]] bool isPickerOpen() const { return static_cast<bool>(_paletteOverlay); }

    [[nodiscard]] glm::vec2 computeIntrinsicSize() const override;
    void onAttached() override;

    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "colorEdit"},
                           {"color", {_color.r, _color.g, _color.b, _color.a}},
                           {"mixed", _bMixed},
                           {"pickerOpen", isPickerOpen()}};
    }
    void clearTransientInputState() override { closePalette(); }

  protected:
    void construct() override;

  private:
    [[nodiscard]] int visibleChannelCount() const;
    [[nodiscard]] std::string channelStyleKey() const;
    void syncChannelsFromColor();
    void syncChannelVisibility();
    void applyHostPadding();
    void openPalette();
    void closePalette();

    std::shared_ptr<UIElement>   _swatch;
    std::array<std::shared_ptr<UIDragFloat>, 4> _channels{};
    bool       _bMixed   = false;
    bool       _bSyncing = false;
    std::shared_ptr<UIPopupOverlay> _paletteOverlay;
};

} // namespace ya
