#pragma once

// ============================================================================
// Reactive - event-driven reactive value binding (Vue semantics, minimal).
//
// Binding-layer fact source (G4.1): Reactive belongs to GUI dataflow/binding,
// not the widget kernel itself.
// ============================================================================

#include "Core/Api.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ya
{

struct UIElement;
class ReactiveBase;

struct ReactiveListDiff
{
    std::vector<size_t> inserted;
    std::vector<size_t> removed;
    std::vector<size_t> moved;
    std::vector<size_t> updated;
    bool empty() const { return inserted.empty() && removed.empty() && moved.empty() && updated.empty(); }
};

YA_GUI_API void trackReactiveDependency(ReactiveBase* ref, UIElement* widget);
YA_GUI_API void pushPaintWidget(UIElement* widget);
YA_GUI_API void popPaintWidget();
YA_GUI_API UIElement* currentPaintWidget();
YA_GUI_API void pushTrackingComputed(ReactiveBase* ref);
YA_GUI_API void popTrackingComputed();
YA_GUI_API ReactiveBase* currentTrackingComputed();

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
    uint64_t computedRecomputes = 0;
    uint64_t computedCycles = 0;
    uint64_t computedUpstreamUnlinks = 0;
    uint64_t wrongThreadMutations = 0;
    uint64_t deferredReentrantNotifications = 0;
};

YA_GUI_API ReactiveDiagnostics getReactiveDiagnostics();
YA_GUI_API bool validateReactiveMutationThread();
YA_GUI_API void recordComputedCycle();
YA_GUI_API void recordComputedRecompute();

/// Synchronous UI-side mutation transaction. Reactive writes are coalesced
/// until the outermost scope exits; no cross-thread queue is implied.
class YA_GUI_API ReactiveTransaction
{
public:
    ReactiveTransaction();
    ~ReactiveTransaction();
    ReactiveTransaction(const ReactiveTransaction&) = delete;
    ReactiveTransaction& operator=(const ReactiveTransaction&) = delete;
};

class YA_GUI_API ReactiveBase
{
public:
    enum class EDirtyLevel : uint8_t
    {
        Paint,
        Layout,
    };

    virtual ~ReactiveBase();
    [[nodiscard]] uint64_t revision() const { return _revision; }

    void addPaintDependent(UIElement* widget, EDirtyLevel level);
    void removePaintDependent(UIElement* widget);

    void addPersistentDependent(UIElement* widget, EDirtyLevel level);
    void removePersistentDependent(UIElement* widget);
    void addComputedDependent(ReactiveBase* ref);
    void removeComputedDependent(ReactiveBase* ref);

    void notifyDependents();

    virtual void registerUpstream(ReactiveBase& ref) { (void)ref; }
    virtual void unregisterUpstream(ReactiveBase* ref) { (void)ref; }
    virtual void markComputedDirty() {}

private:
    void markChanged() { ++_revision; }
    struct Dependent
    {
        UIElement*  widget;
        EDirtyLevel level;
    };

    std::vector<Dependent> _paintDependents;
    std::vector<Dependent> _persistentDependents;
    std::vector<ReactiveBase*> _computedDependents;
    uint64_t _revision = 0;

protected:
    void changed() { markChanged(); }
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
        if (ReactiveBase* computed = currentTrackingComputed()) {
            Reactive* self = const_cast<Reactive*>(this);
            self->addComputedDependent(computed);
            computed->registerUpstream(*self);
        }
        return _value;
    }

    const T& value() const { return _value; }

    void set(T value)
    {
        if (!validateReactiveMutationThread()) return;
        if (_value == value) {
            return;
        }
        _value = std::move(value);
        changed();
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
    using Key = std::string;
    using KeyOf = std::function<Key(const T&)>;
    using ValueEqual = std::function<bool(const T&, const T&)>;
    [[nodiscard]] size_t size(EDirtyLevel level = EDirtyLevel::Paint) const
    {
        if (UIElement* widget = currentPaintWidget()) {
            ReactiveList* self = const_cast<ReactiveList*>(this);
            self->addPaintDependent(widget, level);
            trackReactiveDependency(self, widget);
        }
        if (ReactiveBase* computed = currentTrackingComputed()) {
            ReactiveList* self = const_cast<ReactiveList*>(this);
            self->addComputedDependent(computed);
            computed->registerUpstream(*self);
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
        if (ReactiveBase* computed = currentTrackingComputed()) {
            ReactiveList* self = const_cast<ReactiveList*>(this);
            self->addComputedDependent(computed);
            computed->registerUpstream(*self);
        }
        return _items[index];
    }

    bool push(const T& item)
    {
        if (!validateReactiveMutationThread()) return false;
        if (_keyOf) {
            const Key key = _keyOf(item);
            if (key.empty() || containsKey(key)) return false;
        }
        _items.push_back(item);
        changed();
        _lastDiff = {};
        _lastDiff.inserted.push_back(_items.size() - 1);
        notifyDependents();
        return true;
    }

    bool removeAt(size_t index)
    {
        if (!validateReactiveMutationThread() || index >= _items.size()) return false;
        _items.erase(_items.begin() + static_cast<ptrdiff_t>(index));
        changed();
        _lastDiff = {};
        _lastDiff.removed.push_back(index);
        notifyDependents();
        return true;
    }

    bool insertAt(size_t index, const T& item)
    {
        if (!validateReactiveMutationThread() || index > _items.size()) return false;
        if (_keyOf) {
            const Key key = _keyOf(item);
            if (key.empty() || containsKey(key)) return false;
        }
        _items.insert(_items.begin() + static_cast<ptrdiff_t>(index), item);
        changed();
        _lastDiff = {};
        _lastDiff.inserted.push_back(index);
        notifyDependents();
        return true;
    }

    bool updateAt(size_t index, T item)
    {
        if (!validateReactiveMutationThread() || index >= _items.size()) return false;
        if (_keyOf && _keyOf(_items[index]) != _keyOf(item)) return false;
        _items[index] = std::move(item);
        changed();
        _lastDiff = {};
        _lastDiff.updated.push_back(index);
        notifyDependents();
        return true;
    }

    bool move(size_t from, size_t to)
    {
        if (!validateReactiveMutationThread() || from >= _items.size() || to >= _items.size()) return false;
        if (from == to) return true;
        T item = std::move(_items[from]);
        _items.erase(_items.begin() + static_cast<ptrdiff_t>(from));
        _items.insert(_items.begin() + static_cast<ptrdiff_t>(to), std::move(item));
        changed();
        _lastDiff = {};
        _lastDiff.moved.push_back(to);
        notifyDependents();
        return true;
    }

    void clear()
    {
        if (!validateReactiveMutationThread()) return;
        if (_items.empty()) return;
        _lastDiff = {};
        _lastDiff.removed.reserve(_items.size());
        for (size_t i = 0; i < _items.size(); ++i) _lastDiff.removed.push_back(i);
        _items.clear();
        changed();
        notifyDependents();
    }

    void replace(std::vector<T> items)
    {
        if (!validateReactiveMutationThread()) return;
        _items = std::move(items);
        changed();
        _keyOf = {};
        _lastDiff = {};
        notifyDependents();
    }

    /// Replace a keyed collection and expose structural changes so list
    /// consumers can preserve row identity instead of rebuilding by position.
    /// Keys must be unique; invalid input is rejected without mutating state.
    bool replaceKeyed(std::vector<T> items, KeyOf keyOf, ValueEqual equal = {})
    {
        if (!validateReactiveMutationThread()) return false;
        std::vector<Key> oldKeys;
        std::vector<Key> newKeys;
        oldKeys.reserve(_items.size());
        newKeys.reserve(items.size());
        for (const T& item : _items) oldKeys.push_back(_keyOf ? _keyOf(item) : std::string{});
        for (const T& item : items) newKeys.push_back(keyOf(item));
        std::unordered_set<Key> seen;
        for (const Key& key : newKeys) {
            if (key.empty() || !seen.insert(key).second) return false;
        }
        ReactiveListDiff diff;
        for (size_t i = 0; i < oldKeys.size(); ++i) {
            auto it = std::find(newKeys.begin(), newKeys.end(), oldKeys[i]);
            if (it == newKeys.end()) diff.removed.push_back(i);
        }
        for (size_t i = 0; i < newKeys.size(); ++i) {
            auto it = std::find(oldKeys.begin(), oldKeys.end(), newKeys[i]);
            if (it == oldKeys.end()) diff.inserted.push_back(i);
            else {
                const size_t oldIndex = static_cast<size_t>(std::distance(oldKeys.begin(), it));
                if (oldIndex != i) diff.moved.push_back(i);
                if (!equal || !equal(_items[oldIndex], items[i])) diff.updated.push_back(i);
            }
        }
        _items = std::move(items);
        _keyOf = std::move(keyOf);
        changed();
        _lastDiff = std::move(diff);
        if (!_lastDiff.empty()) notifyDependents();
        return true;
    }

    [[nodiscard]] const ReactiveListDiff& lastDiff() const { return _lastDiff; }
    [[nodiscard]] bool isKeyed() const { return static_cast<bool>(_keyOf); }
    [[nodiscard]] Key keyAt(size_t index) const
    {
        if (!_keyOf || index >= _items.size()) return {};
        return _keyOf(_items[index]);
    }
    [[nodiscard]] size_t indexOfKey(const Key& key) const
    {
        if (!_keyOf || key.empty()) return _items.size();
        for (size_t i = 0; i < _items.size(); ++i) {
            if (_keyOf(_items[i]) == key) return i;
        }
        return _items.size();
    }

private:
    [[nodiscard]] bool containsKey(const Key& key) const
    {
        if (!_keyOf) return false;
        for (const T& item : _items) if (_keyOf(item) == key) return true;
        return false;
    }

    std::vector<T> _items;
    KeyOf _keyOf;
    ReactiveListDiff _lastDiff;
};

template <typename T>
class Computed final : public ReactiveBase
{
public:
    using Selector = std::function<T()>;

    explicit Computed(Selector selector) : _selector(std::move(selector)) {}
    ~Computed() override
    {
        for (ReactiveBase* upstream : _upstreams) {
            if (upstream) {
                upstream->removeComputedDependent(this);
            }
        }
    }

    const T& get(EDirtyLevel level = EDirtyLevel::Paint) const
    {
        if (UIElement* widget = currentPaintWidget()) {
            Computed* self = const_cast<Computed*>(this);
            self->addPaintDependent(widget, level);
            trackReactiveDependency(self, widget);
        }
        if (ReactiveBase* computed = currentTrackingComputed()) {
            Computed* self = const_cast<Computed*>(this);
            self->addComputedDependent(computed);
            computed->registerUpstream(*self);
        }
        Computed* self = const_cast<Computed*>(this);
        self->recomputeIfNeeded();
        return _cache;
    }

    void registerUpstream(ReactiveBase& ref) override
    {
        for (ReactiveBase* upstream : _upstreams) {
            if (upstream == &ref) {
                return;
            }
        }
        _upstreams.push_back(&ref);
    }

    void unregisterUpstream(ReactiveBase* ref) override
    {
        _upstreams.erase(
            std::remove(_upstreams.begin(), _upstreams.end(), ref),
            _upstreams.end());
    }

    void markComputedDirty() override
    {
        if (_bDirty) {
            return;
        }
        _bDirty = true;
        notifyDependents();
    }

private:
    void recomputeIfNeeded()
    {
        if (!_bDirty) {
            return;
        }
        if (_bComputing) {
            recordComputedCycle();
            return;
        }
        _bComputing = true;
        for (ReactiveBase* upstream : _upstreams) {
            if (upstream) {
                upstream->removeComputedDependent(this);
            }
        }
        _upstreams.clear();
        pushTrackingComputed(this);
        recordComputedRecompute();
        _cache = _selector();
        popTrackingComputed();
        _bDirty = false;
        _bComputing = false;
    }

    Selector    _selector;
    mutable T   _cache{};
    std::vector<ReactiveBase*> _upstreams;
    bool _bDirty = true;
    bool _bComputing = false;
};

} // namespace ya
