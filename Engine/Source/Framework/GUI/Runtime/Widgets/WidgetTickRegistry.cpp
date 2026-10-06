#include "GUI/Widgets/WidgetTickRegistry.h"

#include "Core/Log.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <numeric>
#include <span>
#include <utility>

namespace ya
{

void FWidgetTickRegistry::add(UIElement& owner, IUITickable* tickable)
{
    const bool bPresent = std::ranges::any_of(_entries, [&](const FWidgetTickEntry& entry) {
        return entry.bLive && entry.owner == &owner && entry.tickable == tickable;
    });
    if (bPresent) {
        return;
    }
    _entries.push_back(FWidgetTickEntry{.owner = &owner, .tickable = tickable, .bLive = true, .bVisible = owner.isVisibleInTree()});
    ++_liveCount;
    _bOrderDirty = true;
}

void FWidgetTickRegistry::remove(const UIElement& owner, const IUITickable* tickable)
{
    for (size_t i = 0; i < _entries.size(); ++i) {
        const FWidgetTickEntry& entry = _entries[i];
        if (entry.bLive && entry.owner == &owner && entry.tickable == tickable) {
            kill(i);
            compact();
            return;
        }
    }
}

void FWidgetTickRegistry::set(UIElement& owner, IUITickable* tickable, bool bEnabled)
{
    if (bEnabled) {
        add(owner, tickable);
    }
    else {
        remove(owner, tickable);
    }
}

void FWidgetTickRegistry::addOwner(UIElement& owner)
{
    if (owner.isTickEnabled()) {
        add(owner, nullptr);
    }
    for (IUITickable* tickable : owner.behaviorsOf<IUITickable>()) {
        if (tickable->isTickEnabled()) {
            add(owner, tickable);
        }
    }
}

void FWidgetTickRegistry::removeNonMembers(const WidgetTree& tree)
{
    for (size_t i = 0; i < _entries.size(); ++i) {
        if (_entries[i].bLive && _entries[i].owner->getTree() != &tree) {
            kill(i);
        }
    }
    compact();
}

void FWidgetTickRegistry::dispatch(const WidgetTree& tree, float deltaSeconds)
{
    if (_bDispatching) {
        YA_CORE_WARN("WidgetTree::tick: re-entrant tick ignored");
        return;
    }
    if (_entries.empty()) {
        return;
    }
    if (_bOrderDirty) {
        sortPreorder();
        _bOrderDirty = false;
    }
    _bDispatching = true;
    const size_t count = _entries.size();
    for (size_t i = 0; i < count; ++i) {
        // A tick that hides or detaches a widget moves the revision; later
        // entries see the new state within the same frame.
        if (_visibilityRevision != tree.getVisibilityRevision()) {
            refreshVisibility(tree.getVisibilityRevision());
        }
        // By value: a callback may register more entries and grow the vector.
        const FWidgetTickEntry entry = _entries[i];
        if (!entry.bLive || !entry.bVisible) {
            continue;
        }
        if (entry.tickable) {
            entry.tickable->tick(*entry.owner, deltaSeconds);
        }
        else {
            entry.owner->tick(deltaSeconds);
        }
    }
    _bDispatching = false;
    compact();
}

void FWidgetTickRegistry::kill(size_t index)
{
    _entries[index].bLive = false;
    _bHasDead             = true;
    --_liveCount;
}

void FWidgetTickRegistry::compact()
{
    if (_bDispatching || !_bHasDead) {
        return;
    }
    std::erase_if(_entries, [](const FWidgetTickEntry& entry) { return !entry.bLive; });
    _bHasDead = false;
}

void FWidgetTickRegistry::sortPreorder()
{
    // Key = (child indices from the root down to the owner, slot within the
    // owner: 0 = own tick, 1 + i = i-th tickable behaviour). An ancestor's path
    // is a prefix of its descendants' and sorts first, so this order is pre-order.
    std::vector<std::pair<std::vector<uint32_t>, uint32_t>> keys(_entries.size());
    for (size_t i = 0; i < _entries.size(); ++i) {
        const FWidgetTickEntry& entry = _entries[i];
        std::vector<uint32_t>&  path  = keys[i].first;
        for (const UIElement* node = entry.owner; node->getParent() != nullptr; node = node->getParent()) {
            const std::vector<UIElementRef>& siblings = node->getParent()->getChildren();
            const auto at = std::ranges::find_if(siblings, [node](const UIElementRef& child) { return child.get() == node; });
            path.push_back(static_cast<uint32_t>(at - siblings.begin()));
        }
        std::ranges::reverse(path);
        if (entry.tickable) {
            const std::span<IUITickable* const> tickables = entry.owner->behaviorsOf<IUITickable>();
            keys[i].second = 1 + static_cast<uint32_t>(std::ranges::find(tickables, entry.tickable) - tickables.begin());
        }
    }
    std::vector<size_t> order(_entries.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::ranges::sort(order, [&keys](size_t a, size_t b) { return keys[a] < keys[b]; });
    std::vector<FWidgetTickEntry> sorted;
    sorted.reserve(_entries.size());
    for (const size_t index : order) {
        sorted.push_back(_entries[index]);
    }
    _entries = std::move(sorted);
}

void FWidgetTickRegistry::refreshVisibility(uint64_t revision)
{
    for (FWidgetTickEntry& entry : _entries) {
        if (entry.bLive) {
            entry.bVisible = entry.owner->isVisibleInTree();
        }
    }
    _visibilityRevision = revision;
}

} // namespace ya
