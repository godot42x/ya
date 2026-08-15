#include "GUI/Widgets/Style.h"

#include "GUI/Widgets/UIElement.h"

namespace ya
{

std::shared_ptr<Reactive<FWidgetStyle>> UIStyleSet::define(std::string name, FWidgetStyle style)
{
    auto handle          = std::make_shared<Reactive<FWidgetStyle>>(std::move(style));
    _styles[std::move(name)] = handle;
    return handle;
}

std::shared_ptr<Reactive<FWidgetStyle>> UIStyleSet::find(const std::string& name) const
{
    const auto it = _styles.find(name);
    return it != _styles.end() ? it->second : nullptr;
}

void UIStyleSet::bindTo(std::shared_ptr<Reactive<FWidgetStyle>> style, UIElement& widget)
{
    if (!style) {
        return;
    }
    style->addPersistentDependent(&widget, ReactiveBase::EDirtyLevel::Paint);
    widget.trackPersistentDependency(style.get());
}

} // namespace ya
