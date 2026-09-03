#include "GUI/Binding/Reactive.h"

#include "Core/Log.h"
#include "GUI/Widgets/UIElement.h"

#include <algorithm>
#include <thread>
#include <vector>

namespace ya
{

namespace
{
std::vector<UIElement*> s_paintStack;
std::vector<ReactiveBase*> s_computedStack;
ReactiveDiagnostics     s_diagnostics;
uint32_t                s_transactionDepth = 0;
uint32_t                s_notificationDepth = 0;
bool                    s_flushing = false;
std::vector<ReactiveBase*> s_pending;
bool                    s_hasMutationThread = false;
std::thread::id         s_mutationThread;
}

namespace
{
void flushPendingIfIdle()
{
    if (s_flushing || s_transactionDepth != 0 || s_notificationDepth != 0) {
        return;
    }
    s_flushing = true;
    while (!s_pending.empty()) {
        std::vector<ReactiveBase*> pending;
        pending.swap(s_pending);
        for (ReactiveBase* ref : pending) {
            if (ref != nullptr) {
                ref->notifyDependents();
            }
        }
    }
    s_flushing = false;
}
} // namespace

bool validateReactiveMutationThread()
{
    const std::thread::id current = std::this_thread::get_id();
    if (!s_hasMutationThread) {
        s_hasMutationThread = true;
        s_mutationThread = current;
        return true;
    }
    if (s_mutationThread == current) {
        return true;
    }
    ++s_diagnostics.wrongThreadMutations;
    YA_CORE_ERROR("Reactive mutation rejected from a non-UI thread");
    return false;
}

ReactiveTransaction::ReactiveTransaction()
{
    if (!validateReactiveMutationThread()) {
        return;
    }
    ++s_transactionDepth;
}

ReactiveTransaction::~ReactiveTransaction()
{
    if (s_transactionDepth == 0) {
        return;
    }
    if (--s_transactionDepth != 0 || s_flushing) {
        return;
    }
    flushPendingIfIdle();
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

void pushTrackingComputed(ReactiveBase* ref)
{
    s_computedStack.push_back(ref);
}

void popTrackingComputed()
{
    s_computedStack.pop_back();
}

ReactiveBase* currentTrackingComputed()
{
    return s_computedStack.empty() ? nullptr : s_computedStack.back();
}

ReactiveDiagnostics getReactiveDiagnostics()
{
    return s_diagnostics;
}

ReactiveBase::~ReactiveBase()
{
    s_pending.erase(std::remove(s_pending.begin(), s_pending.end(), this), s_pending.end());
    const auto computedDependents = _computedDependents;
    for (ReactiveBase* dependent : computedDependents) {
        if (dependent != nullptr) {
            dependent->unregisterUpstream(this);
            ++s_diagnostics.computedUpstreamUnlinks;
        }
    }
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
            return;
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
            return;
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

void ReactiveBase::addComputedDependent(ReactiveBase* ref)
{
    if (ref == nullptr) {
        return;
    }
    for (ReactiveBase* dependent : _computedDependents) {
        if (dependent == ref) {
            return;
        }
    }
    _computedDependents.push_back(ref);
}

void ReactiveBase::removeComputedDependent(ReactiveBase* ref)
{
    _computedDependents.erase(
        std::remove(_computedDependents.begin(), _computedDependents.end(), ref),
        _computedDependents.end());
}

void ReactiveBase::notifyDependents()
{
    if ((s_transactionDepth != 0 || s_notificationDepth != 0) && !s_flushing) {
        if (std::find(s_pending.begin(), s_pending.end(), this) == s_pending.end()) {
            s_pending.push_back(this);
            if (s_notificationDepth != 0) {
                ++s_diagnostics.deferredReentrantNotifications;
            }
        }
        return;
    }
    ++s_notificationDepth;
    ++s_diagnostics.notifyCalls;
    s_diagnostics.dependentVisits += _paintDependents.size() + _persistentDependents.size();
    const auto paintDependents = _paintDependents;
    const auto persistentDependents = _persistentDependents;
    for (const Dependent& d : paintDependents) {
        if (d.level == EDirtyLevel::Paint) {
            d.widget->markPaintDirty(EUIInvalidationReason::ReactivePaint);
        }
        else {
            d.widget->markLayoutDirty(EUIInvalidationReason::ReactiveLayout);
        }
    }
    for (const Dependent& d : persistentDependents) {
        if (d.level == EDirtyLevel::Paint) {
            d.widget->markPaintDirty(EUIInvalidationReason::ReactivePaint);
        }
        else {
            d.widget->markLayoutDirty(EUIInvalidationReason::ReactiveLayout);
        }
    }
    const auto computedDependents = _computedDependents;
    for (ReactiveBase* dependent : computedDependents) {
        if (dependent != nullptr) {
            dependent->markComputedDirty();
        }
    }
    YA_CORE_ASSERT(s_notificationDepth > 0, "Reactive notification depth underflow");
    --s_notificationDepth;
    flushPendingIfIdle();
}

void recordComputedCycle()
{
    ++s_diagnostics.computedCycles;
    YA_CORE_ERROR("Computed cycle detected during selector evaluation");
}

void recordComputedRecompute()
{
    ++s_diagnostics.computedRecomputes;
}

void trackReactiveDependency(ReactiveBase* ref, UIElement* widget)
{
    widget->trackPaintDependency(ref);
}

} // namespace ya
