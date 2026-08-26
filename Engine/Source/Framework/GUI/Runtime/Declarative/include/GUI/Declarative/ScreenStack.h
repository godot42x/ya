#pragma once

// ============================================================================
// ScreenStack - owns the mount/unmount + z-order + input-blocking policy for a
// set of UIScreens over a single WidgetTree.
//
// Screens are ordered by zOrder (descending); the frontmost screen is top().
// Input routing: when the frontmost mounted screen is EInputBlocking::Block
// (modal), every game-layer event is consumed (HandledExclusive); otherwise the
// underlying tree's route result passes through unchanged.
//
// This is the pure policy/composition layer. Wiring into a host frame loop
// (mount vs buildSnapshot, scene lifecycle) is a later slice.
// ============================================================================

#include "GUI/Declarative/UIScreen.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace ya::ui
{

class YA_GUI_API ScreenStack final
{
  public:
    explicit ScreenStack(WidgetTree& tree)
        : _tree(tree)
    {
    }

    /// Mount `screen` and insert it by zOrder. Takes ownership via shared_ptr.
    void push(std::shared_ptr<UIScreen> screen, WidgetTree::ELayer layer = WidgetTree::ELayer::Content);

    /// Unmount + drop the frontmost screen.
    void pop();

    /// Unmount + drop a specific screen (by identity).
    void remove(UIScreen& screen);

    /// Number of mounted screens.
    [[nodiscard]] size_t size() const { return _screens.size(); }
    [[nodiscard]] bool   empty() const { return _screens.empty(); }

    /// Frontmost screen (highest zOrder); nullptr when empty.
    [[nodiscard]] UIScreen* top();

    /// Resolve the final route result for a game-layer event given what the
    /// underlying tree returned. A modal (Block) frontmost screen consumes the
    /// event entirely; otherwise the tree's result passes through.
    [[nodiscard]] EWidgetRouteResult routeEvent(EWidgetRouteResult childResult) const;

  private:
    struct Entry
    {
        std::shared_ptr<UIScreen> screen;
        WidgetTree::ELayer        layer = WidgetTree::ELayer::Content;
    };

    void sortByZOrder();

    WidgetTree&              _tree;
    std::vector<Entry>      _screens;
};

} // namespace ya::ui
