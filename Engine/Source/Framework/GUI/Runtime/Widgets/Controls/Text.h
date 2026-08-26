#pragma once

#include "GUI/Widgets/Reactive.h"
#include "GUI/Widgets/Style.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <memory>

namespace ya
{

struct Font;

/// Text element rendered through the font atlas.
struct YA_GUI_API UIText : public UIElement
{
    YA_REFLECT_BEGIN(UIText, UIElement)
    YA_REFLECT_FIELD(_text, .instanceEditable())
    YA_REFLECT_FIELD(_fontSize, .instanceEditable())
    YA_REFLECT_FIELD(_color, .instanceEditable())
    YA_REFLECT_FIELD(_hAlign, .instanceEditable())
    YA_REFLECT_FIELD(_vAlign, .instanceEditable())
    YA_REFLECT_END()

    explicit UIText(std::string name = "Text") : UIElement(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIText>; }

    // Runtime mutable text (GI-202): presenters/status log update this per
    // frame, so it is a protected backing field with a changed-only setter
    // and a getter.
  protected:
    std::string _text = "Text";
  public:
    // Authoring-only (GI-202 exception list): base values set once at
    // construction/deserialization; fontSize/color are overridable at runtime
    // (via a mounted theme for un-authored text). To be encapsulated when
    // they gain a setter.
    uint32_t      _fontSize = 16;
    glm::vec4     _color    = {1.0f, 1.0f, 1.0f, 1.0f};
    EWidgetAlignH _hAlign   = EWidgetAlignH::Left;
    EWidgetAlignV _vAlign   = EWidgetAlignV::Top;
    /// Opt-in: draw the resolved style's fillColor as the text background
    /// (padded by the style's padding). Off by default — plain labels must
    /// stay transparent so existing UI is unaffected. Useful for themed
    /// badge/chip labels driven by a UIStyleSet.
    bool _bFillBackground = false;
    /// Opt-in word/character wrapping (editor-parity P6): when true the
    /// text breaks onto multiple lines so it never exceeds `_maxWrapWidth`
    /// (0 = use the arranged rect width). Wrapped text is painted line by
    /// line and the desired height grows with the line count.
    bool  _bWrap         = false;
    float _maxWrapWidth  = 0.0f;

    /// Changed-only text setter (GI-105): AutoSize text re-measures (Layout)
    /// on a content change; fixed-size text only repaints.
    void setText(const std::string& value)
    {
        if (_text == value) {
            return;
        }
        _text = value;
        invalidateProperty(_bAutoSize ? EUIPropertyImpact::Layout : EUIPropertyImpact::Paint);
    }
    void setFontSize(uint32_t value)
    {
        if (_fontSize == value) {
            return;
        }
        _fontSize = value;
        invalidateProperty(_bAutoSize ? EUIPropertyImpact::Layout : EUIPropertyImpact::Paint);
    }
    void setColor(const glm::vec4& value)
    {
        if (_color == value) {
            return;
        }
        _color = value;
        invalidateProperty(EUIPropertyImpact::Paint);
    }
    [[nodiscard]] const std::string& getText() const { return _text; }
    // SizeToContent: set base UIElement::_bAutoSize to measure the layout
    // rect from the text (desired = text width x lineHeight).

    /// Reactive text binding. When set, paint reads the reactive value (and
    /// records the dependency); set() on the reactive marks this text dirty.
    void bindText(std::shared_ptr<Reactive<std::string>> ref) { _textBinding = std::move(ref); }
    [[nodiscard]] const std::string& resolvedText(ReactiveBase::EDirtyLevel level = ReactiveBase::EDirtyLevel::Paint) const
    {
        return _textBinding ? _textBinding->get(level) : _text;
    }

    /// Resolved text style (theme key first when un-authored, then the
    /// authoring fields). Paint attributes only — never cache the result in
    /// a member (would detach from the reactive dependency graph). The
    /// legacy FWidgetStyle bindStyle path was removed with the Phase 3
    /// cleanup (unified binding path: resolveThemeStyle paint-time get()).
    [[nodiscard]] FTextStyle resolvedStyle(ReactiveBase::EDirtyLevel level = ReactiveBase::EDirtyLevel::Paint) const;

    /// Theme style key (style-system Phase 2/3). When the owning tree has a
    /// theme that defines this key as an FTextStyle AND the text has no
    /// authored color (default white — e.g. button labels), resolvedStyle
    /// reads from it; authored _color wins over the theme (explicit override
    /// first in the resolve chain). Empty key disables theme resolution.
    std::string _styleKey = "text";

    void paintSelf(UIFrameBuilder& builder) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

    /// Greedy wrap: break `text` into lines no wider than `maxWidth`
    /// (measured px). Breaks at whitespace when possible, falls back to
    /// hard character breaks for long tokens (CJK-safe).
    [[nodiscard]] static std::vector<std::string> wrapText(const std::string& text,
                                                           const std::shared_ptr<Font>& font,
                                                           float maxWidth);

  private:
    std::shared_ptr<Reactive<std::string>> _textBinding;
};

} // namespace ya
