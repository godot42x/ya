#pragma once

#include "GUI/Widgets/UIElement.h"

#include <algorithm>
#include <functional>
#include <string>

namespace ya
{

/// Single-line text field (gui-app-bootstrap Phase 3 tool primitive).
///
/// Input contract:
///   - click places the caret at the nearest character boundary and requests
///     focus;
///   - focused field consumes KeyTyped (appends at the caret), Backspace,
///     Left/Right, Home/End and Enter (commit);
///   - `_onTextChanged` fires on every edit, `_onCommit` on Enter / focus
///     loss — the workspace owns the text fact source, the field only edits
///     its own buffer;
///   - detach clears all transient state via the tree contract.
struct YA_GUI_API UITextField : public UIElement
{
    YA_REFLECT_BEGIN(UITextField, UIElement)
    YA_REFLECT_FIELD(_text, .instanceEditable())
    YA_REFLECT_FIELD(_fontSize, .instanceEditable())
    YA_REFLECT_FIELD(_textColor, .instanceEditable())
    YA_REFLECT_FIELD(_backgroundColor, .instanceEditable())
    YA_REFLECT_FIELD(_caretColor, .instanceEditable())
    YA_REFLECT_FIELD(_maxLength, .instanceEditable())
    YA_REFLECT_END()

    explicit UITextField(std::string name = "TextField") : UIElement(std::move(name))
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UITextField>; }

    std::string _text            = "";
    uint32_t    _fontSize        = 16;
    glm::vec4   _textColor       = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4   _backgroundColor = {0.08f, 0.09f, 0.12f, 1.0f};
    glm::vec4   _caretColor      = {0.90f, 0.92f, 0.95f, 1.0f};
    uint32_t    _maxLength       = 256;

    /// Changed-only text setter (GI-105): repaint on a real change. Presenters
    /// replace the buffer from the workspace; a same-value sync is a no-op.
    void setText(const std::string& value)
    {
        if (_text == value) {
            return;
        }
        _text = value;
        invalidateProperty(EUIPropertyImpact::Paint);
    }

    /// Fired on every edit (insert / delete / caret-independent text change).
    std::function<void(const std::string& text)> _onTextChanged;
    /// Fired on Enter and on focus loss (commit the buffer).
    std::function<void(const std::string& text)> _onCommit;

    /// Byte offset of the caret (always on a code-point boundary).
    [[nodiscard]] size_t getCursorIndex() const { return _cursorIndex; }
    /// Clamp the caret into the current buffer (used by presenters when they
    /// replace the text from the workspace).
    void clampCursor() { _cursorIndex = std::min(_cursorIndex, _text.size()); }

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override { return _size; }
    void onFocusGained(bool /*bFromKeyboard*/) override { _bFocused = true; }
    void onFocusLost() override;
    void clearTransientInputState() override { _bFocused = false; }

  private:
    void moveCursorByCodePoint(int direction);
    void erasePreviousCodePoint();
    void insertText(const std::string& text);
    /// Place the caret at the nearest character boundary for `localX`
    /// (tree-local logical px, measured from the field's left edge).
    void placeCaretAt(const glm::vec2& logicalPoint);

    size_t     _cursorIndex = 0;
    VisualFlag _bFocused{*this};
    /// Horizontal scroll offset so the caret stays visible when the text is
    /// wider than the field (recomputed during paint; derived from _text and
    /// _cursorIndex, so it never needs its own invalidation).
    float _scrollX = 0.0f;
};

} // namespace ya
