#pragma once

#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <string>

namespace ya
{

/// Drag-to-adjust numeric value (ImGui DragFloat equivalent, minimal):
/// pointer press starts a capture drag session; horizontal delta adjusts the
/// value by `_speed` per logical pixel, clamped to [_min, _max]. Keyboard
/// Left/Right step by `_speed` * 10 on the focused control.
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
    uint32_t    _fontSize = 13;

    /// Fired on every value change.
    std::function<void(float value)> _onValueChanged;
    /// Pointer capture drag gesture. Undo coalescing opens and closes here,
    /// not on every `_onValueChanged` tick.
    std::function<void()> _onDragBegan;
    std::function<void()> _onDragEnded;

    /// Clamp + notify. Shared by pointer and keyboard paths. `bNotify` is
    /// false for presenter sync so model writes do not re-enter as user edits.
    void setValue(float value, bool bNotify = true);
    void setMixed(bool mixed);
    void setError(bool error);
    [[nodiscard]] bool isMixed() const { return _bMixed; }
    [[nodiscard]] bool hasError() const { return _bError; }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "dragFloat"}, {"value", _value}, {"mixed", _bMixed}, {"error", _bError}};
    }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void onFocusLost() override;
    void clearTransientInputState() override
    {
        const bool bWasDragging = _bDragging;
        _bDragging = false;
        _bEditing  = false;
        _editBuffer.clear();
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
    /// Double-click detection (event timestamps, guardrail G3): a press
    /// within 400ms of the previous one enters text edit mode.
    uint64_t _lastPressTimeMs = 0;
    bool     _bHasLastPress   = false;
    VisualFlag _bEditing{*this};
    std::string _editBuffer;
    /// True right after entering edit mode: the next typed character
    /// replaces the pre-filled buffer (select-all semantics).
    bool _bReplaceNext = false;
};

} // namespace ya
