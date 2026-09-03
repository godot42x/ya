#pragma once

#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <format>
#include <functional>
#include <string>
#include <vector>

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

/// Spin box (ImGui InputInt/InputFloat step equivalent, minimal): three
/// zones — left "-" steps down, right "+" steps up, center shows the value.
/// Clicking a zone repeats on hold (frame-independent via drag capture).
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

    float     _value = 0.0f;
    float     _step  = 1.0f;
    float     _min   = -1000000.0f;
    float     _max   = 1000000.0f;
    uint32_t    _fontSize = 13;

    std::function<void(float value)> _onValueChanged;

    void setValue(float value);

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "spinBox"}, {"value", _value}};
    }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void onFocusLost() override;
    void clearTransientInputState() override
    {
        _hoveredZone = -1;
        _bEditing    = false;
        _editBuffer.clear();
    }

  private:
    enum class EZone : int8_t
    {
        None = -1,
        Minus = 0,
        Plus = 1,
    };
    /// Which zone the pointer is over (0 minus / 1 plus), -1 none.
    int _hoveredZone = -1;
    /// Zone being pressed (capture drag repeat).
    int _pressedZone = -1;
    [[nodiscard]] int zoneFromPointer(float localX) const;
    void stepBy(float multiplier);
    void beginEdit();
    void commitEdit();
    void cancelEdit();
    uint64_t _lastPressTimeMs = 0;
    bool     _bHasLastPress   = false;
    VisualFlag _bEditing{*this};
    std::string _editBuffer;
    /// True right after entering edit mode: the next typed character
    /// replaces the pre-filled buffer (select-all semantics).
    bool _bReplaceNext = false;
};

/// Radio button (minimal): dot + label; selection is managed by the host
/// (like ImGui's shared int*), the control only reports presses and renders
/// its checked state.
struct YA_GUI_API UIRadioButton : public UIElement, public UIStyledWidget<UIRadioButton, FRadioButtonStyle>
{
    YA_REFLECT_BEGIN(UIRadioButton, UIElement)
    YA_REFLECT_FIELD(_bChecked, .instanceEditable())
    YA_REFLECT_FIELD(_label, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FRadioButtonStyle)

    explicit UIRadioButton(std::string name = "RadioButton") : UIElement(std::move(name), "radio")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIRadioButton>; }

    bool        _bChecked = false;
    std::string _label;
    uint32_t    _fontSize = 13;

    /// Fired on click / Space / Enter (the host flips the group's selection).
    std::function<void(UIRadioButton* self)> _onSelect;

    /// Changed-only setter: repaints on a real change.
    void setChecked(bool value)
    {
        if (_bChecked == value) {
            return;
        }
        _bChecked = value;
        invalidateProperty(EUIPropertyImpact::Paint);
    }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "radioButton"}, {"checked", _bChecked}};
    }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    bool isHoverable() const override { return true; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override { _bHovered = false; }
    void resetHoverState() override { onPointerLeave(); }
    void clearTransientInputState() override { _bHovered = false; }

  private:
    VisualFlag _bHovered{*this};
};

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

    void setColor(const glm::vec4& value);

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "colorEdit"},
                               {"color", {_color.r, _color.g, _color.b, _color.a}},
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
    std::shared_ptr<struct UIPopupOverlay> _paletteOverlay;
};

/// Search combo (minimal): a combo whose popup list is filtered by typed
/// text. Focus + KeyTyped updates the filter and re-opens the filtered menu;
/// clicking an entry selects it. The host supplies the items.
struct YA_GUI_API UISearchComboBox : public UIElement, public UIStyledWidget<UISearchComboBox, FSearchComboStyle>
{
    YA_REFLECT_BEGIN(UISearchComboBox, UIElement)
    YA_REFLECT_FIELD(_selectedIndex, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FSearchComboStyle)

    explicit UISearchComboBox(std::string name = "SearchComboBox") : UIElement(std::move(name), "searchcombo")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UISearchComboBox>; }

    std::vector<std::string> _items;
    int      _selectedIndex = -1;
    std::string _filter;
    uint32_t    _fontSize = 13;

    std::function<void(int index)> _onSelectionChanged;

    [[nodiscard]] const std::string& currentLabel() const
    {
        static const std::string kEmpty;
        return _selectedIndex >= 0 && _selectedIndex < static_cast<int>(_items.size())
                   ? _items[_selectedIndex]
                   : kEmpty;
    }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "searchComboBox"},
                               {"selectedIndex", _selectedIndex},
                               {"filter", _filter}};
    }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void onFocusGained(bool /*bFromKeyboard*/) override { _bFocused = true; }
    void onFocusLost() override { _bFocused = false; }
    void clearTransientInputState() override;

  private:
    /// Open (or refresh) the filtered popup menu below the control.
    void openFilteredMenu();
    void closeMenu();
    [[nodiscard]] std::vector<int> filteredIndices() const;

    std::shared_ptr<struct UIMenu> _openMenu;
    /// True while openFilteredMenu refreshes (closing the old menu): the
    /// old menu's dismiss must not clear the filter, only a real close
    /// (pick / outside click / Esc) does.
    bool _bRefreshingMenu = false;
    VisualFlag _bHovered{*this};
    VisualFlag _bFocused{*this};
};

} // namespace ya
