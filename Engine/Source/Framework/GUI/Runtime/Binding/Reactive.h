#pragma once

// ============================================================================
// Reactive - event-driven reactive value binding (Vue semantics, minimal).
//
// Binding-layer fact source (G4.1): Reactive belongs to GUI dataflow/binding,
// not the widget kernel itself. `GUI/Widgets/Reactive.h` remains as a
// compatibility bridge while callers migrate to `GUI/Binding/Reactive.h`.
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

YA_GUI_API void trackReactiveDependency(ReactiveBase* ref, UIElement* widget);
YA_GUI_API void pushPaintWidget(UIElement* widget);
YA_GUI_API void popPaintWidget();
YA_GUI_API UIElement* currentPaintWidget();

class PaintScope
{
public:
    explicit PaintScope(UIElement* widget) { pushPaintWidget(widget); }
    ~PaintScope() { popPaintWidget(); }

    PaintScope(const PaintScope&)            = delete;
    PaintScope& operator=(const PaintScope&) = delete;
};

struct ReactiveDiagnostics
{
    uint64_t notifyCalls     = 0;
    uint64_t dependentVisits = 0;
};

YA_GUI_API ReactiveDiagnostics getReactiveDiagnostics();

class YA_GUI_API ReactiveBase
{
public:
    enum class EDirtyLevel : uint8_t
    {
        Paint,
        Layout,
    };

    virtual ~ReactiveBase();

    void addPaintDependent(UIElement* widget, EDirtyLevel level);
    void removePaintDependent(UIElement* widget);

    void addPersistentDependent(UIElement* widget, EDirtyLevel level);
    void removePersistentDependent(UIElement* widget);

    void notifyDependents();

private:
    struct Dependent
    {
        UIElement*  widget;
        EDirtyLevel level;
    };

    std::vector<Dependent> _paintDependents;
    std::vector<Dependent> _persistentDependents;
};

template <typename T>
class Reactive final : public ReactiveBase
{
public:
    Reactive() = default;
    explicit Reactive(T value) : _value(std::move(value)) {}

    const T& get(EDirtyLevel level = EDirtyLevel::Paint) const
    {
        if (UIElement* widget = currentPaintWidget()) {
            Reactive* self = const_cast<Reactive*>(this);
            self->addPaintDependent(widget, level);
            trackReactiveDependency(self, widget);
        }
        return _value;
    }

    const T& value() const { return _value; }

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

template <typename T>
class ReactiveList final : public ReactiveBase
{
public:
    [[nodiscard]] size_t size(EDirtyLevel level = EDirtyLevel::Paint) const
    {
        if (UIElement* widget = currentPaintWidget()) {
            ReactiveList* self = const_cast<ReactiveList*>(this);
            self->addPaintDependent(widget, level);
            trackReactiveDependency(self, widget);
        }
        return _items.size();
    }

    [[nodiscard]] const T& get(size_t index, EDirtyLevel level = EDirtyLevel::Paint) const
    {
        if (UIElement* widget = currentPaintWidget()) {
            ReactiveList* self = const_cast<ReactiveList*>(this);
            self->addPaintDependent(widget, level);
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

    void replace(std::vector<T> items)
    {
        _items = std::move(items);
        notifyDependents();
    }

private:
    std::vector<T> _items;
};

template <typename T>
class Computed final : public ReactiveBase
{
public:
    using Selector = std::function<T()>;

    explicit Computed(Selector selector) : _selector(std::move(selector)) {}

    const T& get(EDirtyLevel level = EDirtyLevel::Paint) const
    {
        if (UIElement* widget = currentPaintWidget()) {
            Computed* self = const_cast<Computed*>(this);
            self->addPaintDependent(widget, level);
            trackReactiveDependency(self, widget);
        }
        _cache = _selector();
        return _cache;
    }

private:
    Selector    _selector;
    mutable T   _cache{};
};

} // namespace ya
