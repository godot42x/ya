#include "GUI/Widgets/Style.h"

#include "GUI/Widgets/UIElement.h"

namespace ya
{

std::shared_ptr<Reactive<FWidgetStyle>> UIStyleSet::define(std::string name, FWidgetStyle style)
{
    // Guardrail G4: re-defining an existing name mutates the SAME reactive
    // handle (set() notifies every bound widget) instead of replacing it —
    // a replaced handle would silently orphan all existing bindings.
    if (const auto it = _styles.find(name); it != _styles.end()) {
        it->second->set(std::move(style));
        return it->second;
    }
    auto handle = std::make_shared<Reactive<FWidgetStyle>>(std::move(style));
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
