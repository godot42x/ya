#include "GUI/Widgets/UIBehaviorSpec.h"

#include "GUI/Widgets/UIElement.h"

namespace ya
{

void activateBehaviorSpecs(UIElement& root, IUIBehaviorActivator& activator, const FUIBehaviorActivation& context)
{
    for (const FUIBehaviorSpec& spec : root._behaviorSpecs) {
        activator.activate(root, spec, context);
    }
    // Snapshot: an activator may attach or detach children of the node it is
    // given; the walk covers the children the document produced.
    const std::vector<UIElementRef> children = root.getChildren();
    for (const UIElementRef& child : children) {
        if (child) {
            activateBehaviorSpecs(*child, activator, context);
        }
    }
}

} // namespace ya
