#pragma once

// ============================================================================
// UIScreen - a host-owned UI surface: mount/unmount, z-order, input blocking.
//
// A screen owns its live widget subtree (built with ui::build / retained
// attach). It is not a render-function / Description owner. ScreenStack
// composes screens; value updates stay on Reactive bindings.
// ============================================================================

#include "GUI/Widgets/WidgetTree.h"

namespace ya::ui
{

enum class EInputBlocking : uint8_t
{
    PassThrough,
    Block,
};

class YA_GUI_API UIScreen
{
  public:
    UIScreen() = default;
    virtual ~UIScreen() = default;

    UIScreen(const UIScreen&) = delete;
    UIScreen& operator=(const UIScreen&) = delete;

    virtual void onMounted(WidgetTree& tree, WidgetTree::ELayer layer);
    virtual void onUnmounted();
    virtual void onFocus() {}
    virtual void onBlur() {}

    [[nodiscard]] virtual int getZOrder() const { return 0; }
    [[nodiscard]] virtual EInputBlocking getInputBlocking() const { return EInputBlocking::PassThrough; }
    [[nodiscard]] bool isMounted() const { return _bMounted; }

  protected:
    bool _bMounted = false;
};

} // namespace ya::ui
