#pragma once

#include "GUI/Widgets/Reactive.h"
#include "GUI/Widgets/Style.h"
#include "GUI/Widgets/UIElement.h"

#include <memory>

namespace ya
{

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

    std::string       _text      = "Text";
    uint32_t          _fontSize  = 16;
    glm::vec4         _color     = {1.0f, 1.0f, 1.0f, 1.0f};
    EWidgetAlignH     _hAlign    = EWidgetAlignH::Left;
    EWidgetAlignV     _vAlign    = EWidgetAlignV::Top;
    // SizeToContent: set base UIElement::_bAutoSize to measure the layout
    // rect from the text (desired = text width x lineHeight).

    /// Reactive text binding. When set, paint reads the reactive value (and
    /// records the dependency); set() on the reactive marks this text dirty.
    void bindText(std::shared_ptr<Reactive<std::string>> ref) { _textBinding = std::move(ref); }
    [[nodiscard]] const std::string& resolvedText() const
    {
        return _textBinding ? _textBinding->get() : _text;
    }

    /// Style binding: a bound style overrides _color/_fontSize with the
    /// style's textColor/fontSize. Dependency is registered at bind time.
    void bindStyle(std::shared_ptr<Reactive<FWidgetStyle>> style);
    [[nodiscard]] FWidgetStyle resolvedStyle() const;

    void paintSelf(UIFrameBuilder& builder) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  private:
    std::shared_ptr<Reactive<std::string>> _textBinding;
    std::shared_ptr<Reactive<FWidgetStyle>> _styleBinding;
};

} // namespace ya
