#pragma once

// ============================================================================
// Reactive - event-driven reactive value binding (Vue semantics, minimal).
//
// A widget reads a Reactive during its paint walk (get()); that read records
// the widget as a dependent. A later set() walks the dependents and marks
// each paint-dirty, so the next buildSnapshot re-runs only the affected
// widgets instead of rebuilding the whole tree every frame.
//
// Dependency collection is a side effect of the incremental paint walk (the
// per-frame full-tree re-collection anti-pattern is deliberately avoided):
// only widgets that actually re-run their paint re-collect their deps.
// ============================================================================

#include "Core/Api.h"

#include <unordered_set>
#include <utility>
#include <vector>

namespace ya
{

struct UIElement;
class ReactiveBase;

/// Record `ref` as a dependency of `widget` (implemented in Reactive.cpp where
/// UIElement's full definition is available).
YA_GUI_API void trackReactiveDependency(ReactiveBase* ref, UIElement* widget);

/// Paint-walk context stack. The top is the widget currently running its
/// paint; Reactive::get() records it as a dependent. Backed by a module-local
/// static (not an exported data symbol) so the DLL and its consumers share the
/// single stack through these functions across the module boundary.
YA_GUI_API void pushPaintWidget(UIElement* widget);
YA_GUI_API void popPaintWidget();
YA_GUI_API UIElement* currentPaintWidget();

/// Type-erased base so a widget's dependency list can hold heterogeneous
/// Reactive<T> instances. Dependent bookkeeping lives here; value storage and
/// the typed accessors live in Reactive<T>.
class YA_GUI_API ReactiveBase
{
public:
    virtual ~ReactiveBase();

    void addDependent(UIElement* widget) { _dependents.insert(widget); }
    void removeDependent(UIElement* widget) { _dependents.erase(widget); }
    /// Mark every dependent paint-dirty (event-driven invalidation).
    void notifyDependents();

private:
    std::unordered_set<UIElement*> _dependents;
};

template <typename T>
class Reactive final : public ReactiveBase
{
public:
    Reactive() = default;
    explicit Reactive(T value) : _value(std::move(value)) {}

    /// Read the value and record the current painting widget as a dependent.
    const T& get() const
    {
        if (UIElement* widget = currentPaintWidget()) {
            Reactive* self = const_cast<Reactive*>(this);
            self->addDependent(widget);
            trackReactiveDependency(self, widget);
        }
        return _value;
    }

    /// Read without recording a dependency (host/business code reads).
    const T& value() const { return _value; }

    /// Write and notify dependents. No-op when the value is unchanged.
    void set(T value)
    {
        if (_value == value) {
            return;
        }
        _value = std::move(value);
        notifyDependents();
    }

    operator const T&() const { return get(); }

private:
    T _value{};
};

} // namespace ya
