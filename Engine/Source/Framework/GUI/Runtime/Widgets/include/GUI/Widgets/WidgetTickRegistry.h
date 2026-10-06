#pragma once

#include "Core/Common/Types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ya
{

struct UIElement;
struct IUITickable;
struct WidgetTree;

/// One frame registration: an IUITickable behaviour, or (null `tickable`) the
/// owner's own tick(). A live entry's owner is attached to the registry's tree,
/// so it is alive; a dead entry is never dereferenced.
struct FWidgetTickEntry
{
    UIElement*   owner    = nullptr;
    IUITickable* tickable = nullptr;
    bool         bLive    = true;
    bool         bVisible = false;
};

/// The tree's list of everything that wants a frame. Entries join when a
/// registered party becomes an attached member and leave when it detaches or
/// switches off, so an idle tree ticks nothing, regardless of its size.
/// Dispatch order is the tree's pre-order (an owner's own tick before its
/// behaviours, behaviours in attach order), re-derived after structure changes.
/// Registrations may change during dispatch: a removed entry is skipped from
/// then on, an added one runs from the next frame.
class YA_GUI_API FWidgetTickRegistry
{
    std::vector<FWidgetTickEntry> _entries;
    size_t                        _liveCount = 0;
    /// The tree visibility revision the cached bVisible flags were taken at.
    uint64_t                      _visibilityRevision = 0;
    bool                          _bDispatching = false;
    bool                          _bHasDead     = false;
    bool                          _bOrderDirty  = false;

  public:
    void add(UIElement& owner, IUITickable* tickable);
    void remove(const UIElement& owner, const IUITickable* tickable);
    void set(UIElement& owner, IUITickable* tickable, bool bEnabled);
    /// Every enabled registration of `owner` (own tick + tickable behaviours).
    void addOwner(UIElement& owner);
    /// Drop every entry whose owner is no longer a member of `tree`.
    void removeNonMembers(const WidgetTree& tree);
    void markOrderDirty() { _bOrderDirty = true; }

    void dispatch(const WidgetTree& tree, float deltaSeconds);

    [[nodiscard]] size_t size() const { return _liveCount; }

  private:
    void kill(size_t index);
    void compact();
    void sortPreorder();
    void refreshVisibility(uint64_t revision);
};

} // namespace ya
