#pragma once

#include "GameEditor/UI/EditorRootSession.h"
#include "GameRuntime/InputRouter.h"

#include <optional>

namespace ya
{

struct App;
struct EditorLayer;
struct EditorWindowRegistry;
struct EditorWindowSession;
class GUIWindowManager;
class GUIDragRouter;

class EditorInputNode final : public IInputNode
{
  private:
    App*                  _app      = nullptr;
    EditorLayer*          _layer    = nullptr;
    EditorWindowRegistry* _windows  = nullptr;
    GUIWindowManager*     _extraWindows = nullptr;
    GUIDragRouter*        _dragRouter = nullptr;
    EditorWindowId        _windowId = kDefaultEditorWindowId;
    /// RMB look started over the viewport; stays true until RMB release so
    /// mouse-move / WASD still reach InputManager after the pointer leaves.
    bool _bLooking = false;
    /// Last route decided the camera owns the keyboard. Falling this to false
    /// is the only place we cancelHeldKeys (not on every chrome mouse move).
    bool _bFeedingKeys = false;

  public:
    void bind(App& app,
              EditorLayer& layer,
              EditorWindowRegistry& windows,
              EditorWindowId windowId = kDefaultEditorWindowId,
              GUIWindowManager* extraWindows = nullptr,
              GUIDragRouter* dragRouter = nullptr);
    void unbind();

    [[nodiscard]] FInputReply route(FInputRouteContext& context, const FInputEvent& event) override;
    void                     cancelInput(FInputRouteContext& context, EInputCancelReason reason) override;
    [[nodiscard]] std::optional<ECursorType> getCursor() const override;

  private:
    [[nodiscard]] EditorWindowSession* session() const;
    void syncDragRouter(uint32_t primaryNativeId, INativeWindow* native = nullptr);
};

} // namespace ya
