#pragma once

#include "GUI/Declarative/Declarative.h"

namespace ya::ui
{

/// Node-level adapter for the declarative runtime.
///
/// UIReconciler owns tree identity and lifecycle; this adapter owns the
/// open-ended mapping from a UIDescription kind/properties to a retained
/// widget. New declarative controls should extend this boundary instead of
/// adding another kind/property branch to the reconciler itself.
class YA_GUI_API UIDeclarativeNodeAdapter final
{
  public:
    [[nodiscard]] static UIElementRef create(const UIDescription& node);
    static void apply(UIElement& widget, const UIDescription& node);
    [[nodiscard]] static bool sameKind(const UIElement& widget, EWidgetKind kind);
    [[nodiscard]] static const char* kindName(EWidgetKind kind);
};

} // namespace ya::ui
