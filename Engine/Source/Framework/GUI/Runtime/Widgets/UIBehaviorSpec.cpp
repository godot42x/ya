#include "GUI/Widgets/UIBehaviorSpec.h"

#include "GUI/Widgets/UIElement.h"

namespace ya
{
namespace
{

void walkBehaviorSpecs(UIElement& node, IUIBehaviorActivator& activator, const FUIBehaviorActivation& context)
{
    for (const FUIBehaviorSpec& spec : node._behaviorSpecs) {
        activator.activate(node, spec, context);
    }
    // Snapshot: an activator may attach or detach children of the node it is
    // given; the walk covers the children the document produced.
    const std::vector<UIElementRef> children = node.getChildren();
    for (const UIElementRef& child : children) {
        if (child) {
            walkBehaviorSpecs(*child, activator, context);
        }
    }
}

} // namespace

void activateBehaviorSpecs(UIElement& root, IUIBehaviorActivator& activator, const FUIBehaviorActivation& context)
{
    FUIBehaviorActivation scoped = context;
    scoped.documentRoot          = &root;
    walkBehaviorSpecs(root, activator, scoped);
}

} // namespace ya
