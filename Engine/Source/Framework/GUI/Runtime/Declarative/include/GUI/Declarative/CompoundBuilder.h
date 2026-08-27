#pragma once

// Compound builders stay in the native retained authoring layer. They are a
// convenience for local composition roots, not a required host for future
// React-like / HTML-CSS-JS / script adapters.

#include "GUI/Declarative/BuilderBase.h"

namespace ya::ui
{

template<UICompoundWidgetType TWidget, typename... TArgs>
[[nodiscard]] inline TUICompoundWidgetBuilder<TWidget> compound(std::string key,
                                                                std::string displayName = {},
                                                                TArgs&&... args)
{
    return TUICompoundWidgetBuilder<TWidget>{std::move(key),
                                             std::move(displayName),
                                             std::forward<TArgs>(args)...};
}

} // namespace ya::ui
