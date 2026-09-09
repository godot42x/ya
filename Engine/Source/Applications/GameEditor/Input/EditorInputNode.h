#pragma once

#include "GameRuntime/InputRouter.h"

#include <optional>

namespace ya
{

struct App;
struct EditorLayer;
struct EditorWindowSession;

class EditorInputNode final : public IInputNode
{
  private:
    App*                 _app     = nullptr;
    EditorLayer*         _layer   = nullptr;
    EditorWindowSession* _session = nullptr;
    /// RMB look started over the viewport; stays true until RMB release so
    /// mouse-move / WASD still reach InputManager after the pointer leaves.
    bool _bLooking = false;
    /// Last route decided the camera owns the keyboard. Falling this to false
    /// is the only place we cancelHeldKeys (not on every chrome mouse move).
    bool _bFeedingKeys = false;

  public:
    void bind(App& app, EditorLayer& layer, EditorWindowSession* session = nullptr);
    void unbind();

    [[nodiscard]] FInputReply route(FInputRouteContext& context, const FInputEvent& event) override;
    void                     cancelInput(FInputRouteContext& context, EInputCancelReason reason) override;
    [[nodiscard]] std::optional<ECursorType> getCursor() const override;
};

} // namespace ya
