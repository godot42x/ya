#pragma once

#include "GUI/Widgets/Controls/TextEdit.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <string>

namespace ya
{

/// Drag-to-adjust numeric value (ImGui DragFloat equivalent, minimal):
/// pointer press starts a capture drag session; horizontal delta adjusts the
/// value by `_speed` per logical pixel, clamped to [_min, _max]. Keyboard
/// Left/Right step by `_speed` * 10 on the focused control. Double-click
/// (or the edit buffer) reuses `FTextEditState` so selection matches TextField.
struct YA_GUI_API UIDragFloat : public UIElement, public UIStyledWidget<UIDragFloat, FDragFloatStyle>
{
    YA_REFLECT_BEGIN(UIDragFloat, UIElement)
    YA_REFLECT_FIELD(_value, .instanceEditable())
    YA_REFLECT_FIELD(_speed, .instanceEditable())
    YA_REFLECT_FIELD(_min, .instanceEditable())
    YA_REFLECT_FIELD(_max, .instanceEditable())
    YA_REFLECT_FIELD(_decimals, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FDragFloatStyle)

    explicit UIDragFloat(std::string name = "DragFloat") : UIElement(std::move(name), "dragfloat")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIDragFloat>; }

    float     _value    = 0.0f;
    float     _speed    = 0.1f;
    float     _min      = -1000000.0f;
    float     _max      = 1000000.0f;
    int       _decimals = 2;
    uint32_t  _fontSize = 13;

    std::function<void(float value)> _onValueChanged;
    std::function<void()> _onDragBegan;
    std::function<void()> _onDragEnded;

    void setValue(float value, bool bNotify = true);
    void setMixed(bool mixed);
    void setError(bool error);
    [[nodiscard]] bool isMixed() const { return _bMixed; }
    [[nodiscard]] bool hasError() const { return _bError; }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "dragFloat"}, {"value", _value}, {"mixed", _bMixed}, {"error", _bError},
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
        const bool bWasDragging = _bDragging;
        _bDragging = false;
        _bEditing  = false;
        _editBuffer.clear();
        _edit      = {};
        if (bWasDragging && _onDragEnded) {
            _onDragEnded();
        }
    }

  private:
    void adjustValue(float delta);
    void beginEdit();
    void commitEdit();
    void cancelEdit();
    VisualFlag _bDragging{*this};
    bool       _bMixed = false;
    bool       _bError = false;
    glm::vec2  _dragStart{0.0f, 0.0f};
    uint64_t   _lastPressTimeMs = 0;
    bool       _bHasLastPress   = false;
    VisualFlag _bEditing{*this};
    std::string    _editBuffer;
    FTextEditState _edit;
};

} // namespace ya
