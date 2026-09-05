#pragma once

#include "GUI/Widgets/Controls/TextEdit.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <string>

namespace ya
{

/// Spin box (ImGui InputInt/InputFloat step equivalent, minimal): three
/// zones — left "-" steps down, right "+" steps up, center shows the value.
/// Center click / double-click edit reuses `FTextEditState` (same as TextField).
struct YA_GUI_API UISpinBox : public UIElement, public UIStyledWidget<UISpinBox, FSpinBoxStyle>
{
    YA_REFLECT_BEGIN(UISpinBox, UIElement)
    YA_REFLECT_FIELD(_value, .instanceEditable())
    YA_REFLECT_FIELD(_step, .instanceEditable())
    YA_REFLECT_FIELD(_min, .instanceEditable())
    YA_REFLECT_FIELD(_max, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FSpinBoxStyle)

    explicit UISpinBox(std::string name = "SpinBox") : UIElement(std::move(name), "spinbox")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UISpinBox>; }

    float    _value    = 0.0f;
    float    _step     = 1.0f;
    float    _min      = -1000000.0f;
    float    _max      = 1000000.0f;
    uint32_t _fontSize = 13;

    std::function<void(float value)> _onValueChanged;

    void setValue(float value);

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "spinBox"}, {"value", _value},
                           {"editing", static_cast<bool>(_bEditing)}};
    }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void onFocusLost() override;
    [[nodiscard]] ECursorType getCursor() const override
    {
        return _bEditing ? ECursorType::IBeam : ECursorType::Arrow;
    }
    void clearTransientInputState() override
    {
        _hoveredZone = -1;
        _bEditing    = false;
        _editBuffer.clear();
        _edit = {};
    }

  private:
    int _hoveredZone = -1;
    int _pressedZone = -1;
    [[nodiscard]] int zoneFromPointer(float localX) const;
    void stepBy(float multiplier);
    void beginEdit();
    void commitEdit();
    void cancelEdit();
    uint64_t       _lastPressTimeMs = 0;
    bool           _bHasLastPress   = false;
    VisualFlag     _bEditing{*this};
    std::string    _editBuffer;
    FTextEditState _edit;
};

} // namespace ya
