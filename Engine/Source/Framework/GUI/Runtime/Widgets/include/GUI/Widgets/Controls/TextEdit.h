#pragma once

#include "Core/Api.h"
#include "Core/Common/Types.h"
#include "GUI/Layout/UILayoutTypes.h"
#include "GUI/Widgets/UIElement.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace ya
{

struct Font;
struct KeyPressedEvent;
class UIFrameBuilder;
struct WidgetTree;

/// Shared single-line edit caret + selection (byte offsets on UTF-8
/// boundaries). TextField, DragFloat, and SpinBox edit-mode all use this
/// instead of a third mini-editor.
struct FTextEditState
{
    size_t caret  = 0;
    size_t anchor = 0;

    [[nodiscard]] bool   hasSelection() const { return caret != anchor; }
    [[nodiscard]] size_t selectionStart() const { return caret < anchor ? caret : anchor; }
    [[nodiscard]] size_t selectionEnd() const { return caret < anchor ? anchor : caret; }

    void clamp(size_t textSize)
    {
        caret  = std::min(caret, textSize);
        anchor = std::min(anchor, textSize);
    }
    void collapseToCaret() { anchor = caret; }
    void selectAll(size_t textSize)
    {
        anchor = 0;
        caret  = textSize;
    }
    void setCaret(size_t byteIndex, bool bExtendSelection)
    {
        caret = byteIndex;
        if (!bExtendSelection) {
            anchor = caret;
        }
    }
};

inline constexpr glm::vec4 kTextEditSelectionColor{0.24f, 0.46f, 0.82f, 0.45f};

YA_GUI_API size_t textEditPreviousCodePoint(const std::string& text, size_t byteIndex);
YA_GUI_API size_t textEditNextCodePoint(const std::string& text, size_t byteIndex);
YA_GUI_API std::string textEditSanitizeSingleLine(std::string_view text);

YA_GUI_API bool textEditDeleteSelection(std::string& text, FTextEditState& state);
YA_GUI_API void textEditInsert(std::string&        text,
                               FTextEditState&     state,
                               std::string_view    insert,
                               uint32_t            maxLength);
YA_GUI_API void textEditBackspace(std::string& text, FTextEditState& state);
YA_GUI_API void textEditDeleteForward(std::string& text, FTextEditState& state);
YA_GUI_API void textEditMoveByCodePoint(const std::string& text,
                                        FTextEditState&    state,
                                        int                direction,
                                        bool               bExtend);
YA_GUI_API void textEditMoveHome(FTextEditState& state, bool bExtend);
YA_GUI_API void textEditMoveEnd(const std::string& text, FTextEditState& state, bool bExtend);

/// Clipboard / arrows / Home / End / Backspace / Delete. Enter and Esc stay
/// with the control (commit vs cancel). Repeat C/X/V/A is ignored so the
/// tree can treat them as not handled.
[[nodiscard]] YA_GUI_API bool textEditHandleKey(std::string&            text,
                                                FTextEditState&         state,
                                                const KeyPressedEvent&  key,
                                                WidgetTree*             tree,
                                                uint32_t                maxLength);

YA_GUI_API size_t textEditHitIndex(const std::string&           text,
                                   const std::shared_ptr<Font>& font,
                                   const Rect2D&                rect,
                                   float                        logicalX,
                                   EWidgetAlignH                alignH,
                                   float                        scrollX);

YA_GUI_API void textEditPaint(UIFrameBuilder&             builder,
                              const Rect2D&               rect,
                              const std::string&          text,
                              const FTextEditState&       state,
                              const std::shared_ptr<Font>& font,
                              const glm::vec4&            textColor,
                              const glm::vec4&            caretColor,
                              const glm::vec4&            selectionColor,
                              EWidgetAlignH               alignH,
                              float                       scrollX,
                              bool                        bShowCaret);

} // namespace ya
