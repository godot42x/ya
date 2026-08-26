#include "GUI/Declarative/ScreenStack.h"
#include "GUI/Declarative/UIScreen.h"

#include "Core/Log.h"

#include <algorithm>

namespace ya::ui
{

void UIScreen::onMounted(WidgetTree& tree, WidgetTree::ELayer layer)
{
    (void)tree;
    (void)layer;
    _bMounted = true;
}

void UIScreen::onUnmounted()
{
    _bMounted = false;
}

void ScreenStack::push(std::shared_ptr<UIScreen> screen, WidgetTree::ELayer layer)
{
    YA_CORE_ASSERT(screen, "ScreenStack::push: null screen");
    YA_CORE_ASSERT(!screen->isMounted(), "ScreenStack::push: screen already mounted");
    screen->onMounted(_tree, layer);
    _screens.push_back({std::move(screen), layer});
    sortByZOrder();
}

void ScreenStack::pop()
{
    if (_screens.empty()) {
        return;
    }
    _screens.back().screen->onUnmounted();
    _screens.pop_back();
}

void ScreenStack::remove(UIScreen& screen)
{
    auto it = std::find_if(_screens.begin(), _screens.end(),
                           [&](const Entry& e) { return e.screen.get() == &screen; });
    if (it != _screens.end()) {
        it->screen->onUnmounted();
        _screens.erase(it);
    }
}

UIScreen* ScreenStack::top()
{
    return _screens.empty() ? nullptr : _screens.back().screen.get();
}

EWidgetRouteResult ScreenStack::routeEvent(EWidgetRouteResult childResult) const
{
    if (_screens.empty()) {
        return childResult;
    }
    const UIScreen* front = _screens.back().screen.get();
    if (front->getInputBlocking() == EInputBlocking::Block) {
        return EWidgetRouteResult::HandledExclusive;
    }
    return childResult;
}

void ScreenStack::sortByZOrder()
{
    std::stable_sort(_screens.begin(), _screens.end(), [](const Entry& lhs, const Entry& rhs) {
        return lhs.screen->getZOrder() < rhs.screen->getZOrder();
    });
}

} // namespace ya::ui
