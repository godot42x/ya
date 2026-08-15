#include "GUI/Widgets/Reactive.h"

#include "GUI/Widgets/UIElement.h"

namespace ya
{

namespace
{
std::vector<UIElement*> s_paintStack;
ReactiveDiagnostics     s_diagnostics;
}

void pushPaintWidget(UIElement* widget)
{
    s_paintStack.push_back(widget);
}

void popPaintWidget()
{
    s_paintStack.pop_back();
}

UIElement* currentPaintWidget()
{
    return s_paintStack.empty() ? nullptr : s_paintStack.back();
}

ReactiveDiagnostics getReactiveDiagnostics()
{
    return s_diagnostics;
}

ReactiveBase::~ReactiveBase()
{
    // Sever every widget that read this ref. A widget may outlive the ref
    // (e.g. the ref is released before ~UIElement runs clearDependencies), so
    // the ref must proactively remove itself from each dependent's list.
    for (UIElement* widget : _dependents) {
        widget->untrackDependency(this);
    }
}

void ReactiveBase::notifyDependents()
{
    ++s_diagnostics.notifyCalls;
    s_diagnostics.dependentVisits += _dependents.size();
    for (UIElement* widget : _dependents) {
        if (_dirtyLevel == EDirtyLevel::Paint) {
            widget->markPaintDirty(EUIInvalidationReason::ReactivePaint);
        }
        else {
            widget->markLayoutDirty(EUIInvalidationReason::ReactiveLayout);
        }
    }
}

void trackReactiveDependency(ReactiveBase* ref, UIElement* widget)
{
    widget->trackDependency(ref);
}

} // namespace ya
