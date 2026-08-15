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

#include <functional>
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

/// Process-wide diagnostics for reactive invalidation (GI-001). Lightweight
/// counters (no string allocation on the hot path) used to measure notify and
/// dependent-traversal cost; the Phase 3 batching decision reads these.
struct ReactiveDiagnostics
{
    uint64_t notifyCalls     = 0; // notifyDependents() invocations
    uint64_t dependentVisits = 0; // total dependents walked by notifyDependents
};

YA_GUI_API ReactiveDiagnostics getReactiveDiagnostics();

/// Type-erased base so a widget's dependency list can hold heterogeneous
/// Reactive<T> instances. Dependent bookkeeping lives here; value storage and
/// the typed accessors live in Reactive<T>.
class YA_GUI_API ReactiveBase
{
public:
    /// Invalidation granularity. Paint = re-run only the dependent's paintSelf;
    /// Layout = invalidate the tree's layout (measure + arrange + paint).
    enum class EDirtyLevel : uint8_t
    {
        Paint,
        Layout,
    };

    virtual ~ReactiveBase();

    void addDependent(UIElement* widget) { _dependents.insert(widget); }
    void removeDependent(UIElement* widget) { _dependents.erase(widget); }
    /// Mark every dependent dirty at this ref's configured level.
    void notifyDependents();

    /// Set the invalidation granularity (default Paint). Layout-bound refs
    /// (e.g. split ratio) must opt in so a write re-runs measure/arrange.
    void setDirtyLevel(EDirtyLevel level) { _dirtyLevel = level; }

private:
    std::unordered_set<UIElement*> _dependents;
    EDirtyLevel                    _dirtyLevel = EDirtyLevel::Paint;
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

/// Reactive collection (TreeView data-source precursor). size()/get() record
/// the painting widget as a dependent; push/removeAt/clear notify dependents.
/// Minimal by design: no tree rendering, no virtualized list — just the
/// observable container contract.
template <typename T>
class ReactiveList final : public ReactiveBase
{
public:
    [[nodiscard]] size_t size() const
    {
        if (UIElement* widget = currentPaintWidget()) {
            ReactiveList* self = const_cast<ReactiveList*>(this);
            self->addDependent(widget);
            trackReactiveDependency(self, widget);
        }
        return _items.size();
    }

    [[nodiscard]] const T& get(size_t index) const
    {
        if (UIElement* widget = currentPaintWidget()) {
            ReactiveList* self = const_cast<ReactiveList*>(this);
            self->addDependent(widget);
            trackReactiveDependency(self, widget);
        }
        return _items[index];
    }

    void push(const T& item)
    {
        _items.push_back(item);
        notifyDependents();
    }

    void removeAt(size_t index)
    {
        _items.erase(_items.begin() + static_cast<ptrdiff_t>(index));
        notifyDependents();
    }

    void clear()
    {
        _items.clear();
        notifyDependents();
    }

private:
    std::vector<T> _items;
};

/// Derived reactive value (computed/selector). Interface only for now — the
/// full dependency-aware derived-value engine is a later milestone; declare the
/// shape so call sites don't hard-code Reactive<T> where a derived value should
/// eventually plug in.
template <typename T>
class Computed final : public ReactiveBase
{
public:
    using Selector = std::function<T()>;

    explicit Computed(Selector selector) : _selector(std::move(selector)) {}

    [[nodiscard]] const T& get() const
    {
        if (UIElement* widget = currentPaintWidget()) {
            Computed* self = const_cast<Computed*>(this);
            self->addDependent(widget);
            trackReactiveDependency(self, widget);
        }
        _value = _selector();
        return _value;
    }

private:
    Selector       _selector;
    mutable T      _value{};
};

} // namespace ya
