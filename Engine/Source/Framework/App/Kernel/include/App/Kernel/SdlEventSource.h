#pragma once

#include "Core/Api.h"
#include "App/Kernel/AppKernel.h"

#include <cstdint>

namespace ya
{

/// The native `IAppEventSource` every SDL-windowed product line pumps through.
/// Owns the window-level protocol the hosts used to each re-implement:
///
///   - host-window filtering: events tagged with another window's id are
///     dropped (0 = accept every window, so a multi-window app can route them
///     itself -- see GUIApp::onEvent);
///   - first-pointer synthesis: the tree/input sees a known pointer position
///     before the first real mouse event, from the window-system pointer query;
///   - mouse enter/leave handoff: enter becomes a Move (queried position) +
///     Focus, leave becomes a far Move (hover cleared) followed by the leave
///     itself -- the boundary where pointer sessions are reconciled against
///     the physical button state.
///
/// The window id is the only window fact here: no window handle, no GUI type.
class YA_APP_KERNEL_API SdlEventSource final : public IAppEventSource
{
  public:
    /// 0 = emit every window's events; non-zero = only this window's (and
    /// global, id 0) events.
    void setHostWindowId(uint32_t windowId) { _hostWindowId = windowId; }
    [[nodiscard]] uint32_t hostWindowId() const { return _hostWindowId; }

    void pollEvents(const std::function<void(const Event&)>& emit) override;

  private:
    [[nodiscard]] bool isHostWindow(uint32_t windowId) const
    {
        return _hostWindowId == 0 || windowId == 0 || windowId == _hostWindowId;
    }

    uint32_t _hostWindowId  = 0;
    bool     _bPointerKnown = false;
};

} // namespace ya
