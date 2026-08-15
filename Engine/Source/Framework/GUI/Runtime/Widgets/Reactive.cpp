#include "GUI/Widgets/Reactive.h"

#include "GUI/Widgets/UIElement.h"

#include <algorithm>

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
    // Sever every widget that read or bound this ref. A widget may outlive the
    // ref (e.g. the ref is released before ~UIElement runs its clears), so the
    // ref must proactively remove itself from each dependent's back-list.
    for (const Dependent& d : _paintDependents) {
        d.widget->untrackDependency(this);
    }
    for (const Dependent& d : _persistentDependents) {
        d.widget->untrackDependency(this);
    }
}

void ReactiveBase::addPaintDependent(UIElement* widget, EDirtyLevel level)
{
    for (const Dependent& d : _paintDependents) {
        if (d.widget == widget && d.level == level) {
            return; // already collected at this level this frame
        }
    }
    _paintDependents.push_back({widget, level});
}

void ReactiveBase::removePaintDependent(UIElement* widget)
{
    _paintDependents.erase(
        std::remove_if(_paintDependents.begin(), _paintDependents.end(),
                       [widget](const Dependent& d) { return d.widget == widget; }),
        _paintDependents.end());
}

void ReactiveBase::addPersistentDependent(UIElement* widget, EDirtyLevel level)
{
    for (const Dependent& d : _persistentDependents) {
        if (d.widget == widget && d.level == level) {
            return; // already bound at this level
        }
    }
    _persistentDependents.push_back({widget, level});
}

void ReactiveBase::removePersistentDependent(UIElement* widget)
{
    _persistentDependents.erase(
        std::remove_if(_persistentDependents.begin(), _persistentDependents.end(),
                       [widget](const Dependent& d) { return d.widget == widget; }),
        _persistentDependents.end());
}

void ReactiveBase::notifyDependents()
{
    ++s_diagnostics.notifyCalls;
    s_diagnostics.dependentVisits += _paintDependents.size() + _persistentDependents.size();
    for (const Dependent& d : _paintDependents) {
        if (d.level == EDirtyLevel::Paint) {
            d.widget->markPaintDirty(EUIInvalidationReason::ReactivePaint);
        }
        else {
            d.widget->markLayoutDirty(EUIInvalidationReason::ReactiveLayout);
        }
    }
    for (const Dependent& d : _persistentDependents) {
        if (d.level == EDirtyLevel::Paint) {
            d.widget->markPaintDirty(EUIInvalidationReason::ReactivePaint);
        }
        else {
            d.widget->markLayoutDirty(EUIInvalidationReason::ReactiveLayout);
        }
    }
}

void trackReactiveDependency(ReactiveBase* ref, UIElement* widget)
{
    widget->trackPaintDependency(ref);
}

} // namespace ya
