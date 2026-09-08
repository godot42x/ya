#pragma once

#include "GUI/Widgets/Controls/TextEdit.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <string>

namespace ya
{

/// Single-line text field (gui-app-bootstrap Phase 3 tool primitive).
///
/// Input contract:
///   - click places the caret at the nearest character boundary and requests
///     focus; Shift+click and drag extend the selection; hover shows I-beam;
///   - focused field consumes KeyTyped (insert replaces the selection),
///     Backspace/Delete on the selection or by code point, Left/Right/Home/End
///     (Shift extends), primary+A select-all, Enter (commit);
///   - primary+C/X/V copy/cut/paste through WidgetTree clipboard (OS clipboard
///     is a host hook; paste strips newlines/tabs for the single-line field);
///   - `_onTextChanged` fires on every edit, `_onCommit` on Enter / focus
///     loss — the workspace owns the text fact source, the field only edits
///     its own buffer;
///   - detach clears all transient state via the tree contract.
struct YA_GUI_API UITextField : public UIElement, public UIStyledWidget<UITextField, FTextFieldStyle>
{
    YA_REFLECT_BEGIN(UITextField, UIElement)
    YA_REFLECT_FIELD(_text, .instanceEditable())
    YA_REFLECT_FIELD(_fontSize, .instanceEditable())
    YA_REFLECT_FIELD(_maxLength, .instanceEditable())
    YA_REFLECT_END()

    YA_GUI_AUTHORED_STYLE_IO(FTextFieldStyle)

    explicit UITextField(std::string name = "TextField") : UIElement(std::move(name), "textfield")
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UITextField>; }

    std::string _text            = "";
    uint32_t    _fontSize        = gui_type::kBody;
    uint32_t    _maxLength       = 256;

  public:
    const std::string& getText() const { return _text; }
    /// Changed-only text setter (GI-105): repaint on a real change. Presenters
    /// replace the buffer from the workspace; a same-value sync is a no-op.
    void setText(const std::string& value)
    {
        if (_text == value) {
            return;
        }
        _text = value;
        clampCursor();
        invalidateProperty(EUIPropertyImpact::Layout);
    }

    void setFontSize(uint32_t value)
    {
        if (_fontSize == value) {
            return;
        }
        _fontSize = value;
        setStyleField("fontSize", value, EUIPropertyImpact::Layout);
    }

    void setError(bool error);
    [[nodiscard]] bool hasError() const { return _bError; }

    /// Fired on every edit (insert / delete / caret-independent text change).
    std::function<void(const std::string& text)> _onTextChanged;
    /// Fired on Enter and on focus loss (commit the buffer).
    std::function<void(const std::string& text)> _onCommit;

    /// Byte offset of the caret (always on a code-point boundary).
    [[nodiscard]] size_t getCursorIndex() const { return _edit.caret; }
    [[nodiscard]] size_t getSelectionAnchor() const { return _edit.anchor; }
    [[nodiscard]] bool   hasSelection() const { return _edit.hasSelection(); }
    /// Clamp the caret into the current buffer (used by presenters when they
    /// replace the text from the workspace).
    void clampCursor() { _edit.clamp(_text.size()); }
    [[nodiscard]] ECursorType getCursor() const override { return ECursorType::IBeam; }

    bool isHoverable() const override { return true; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override { _bHovered = false; }
    void resetHoverState() override { _bHovered = false; }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree& tree) const override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override { return computeIntrinsicSize(); }
    [[nodiscard]] glm::vec2 computeIntrinsicSize() const override;
    void onFocusGained(bool /*bFromKeyboard*/) override { _bFocused = true; }
    void onFocusLost() override;
    void clearTransientInputState() override
    {
        _bFocused    = false;
        _bDragSelect = false;
        _bHovered    = false;
        _edit.collapseToCaret();
    }

  private:
    void notifyTextChanged();
    void insertText(const std::string& text);
    /// Place the caret at the nearest character boundary for `logicalPoint`
    /// (tree-local logical px). Shift/drag keeps the existing anchor.
    void placeCaretAt(const glm::vec2& logicalPoint, bool bExtendSelection);
    [[nodiscard]] uint32_t resolvedFontSize() const { return resolvedStyle().fontSize; }
    [[nodiscard]] Rect2D textInnerRect() const;

    FTextEditState _edit;
    VisualFlag     _bFocused{*this};
    VisualFlag     _bHovered{*this};
    bool           _bError      = false;
    bool           _bDragSelect = false;
    /// Horizontal scroll offset so the caret stays visible when the text is
    /// wider than the field (recomputed during paint; derived from _text and
    /// caret, so it never needs its own invalidation).
    float _scrollX = 0.0f;
};

} // namespace ya
