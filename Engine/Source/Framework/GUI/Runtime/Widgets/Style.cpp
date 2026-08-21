#include "GUI/Widgets/Style.h"

#include "GUI/Widgets/UIElement.h"

namespace ya
{

// define/find are header-inline templates (bucketed by type_index); only
// bindTo needs UIElement's full definition and stays here.

void UIStyleSet::bindTo(std::shared_ptr<Reactive<FWidgetStyle>> style, UIElement& widget)
{
    if (!style) {
        return;
    }
    style->addPersistentDependent(&widget, ReactiveBase::EDirtyLevel::Paint);
    widget.trackPersistentDependency(style.get());
}

} // namespace ya
