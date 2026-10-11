#pragma once

#include "Core/Input/InputManager.h"
#include "GameEditor/UI/Shell/EditorRootSession.h"
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
    /// Editor camera and viewport tools. Gameplay keys never land here: Play
    /// forwards those events through App::writeGameplayInput.
    InputManager _editorInput;
    /// RMB look started over the viewport; stays true until RMB release so
    /// mouse-move / WASD still reach the editor input after the pointer leaves.
    bool _bLooking = false;
    /// Last route decided the editor camera owns the keyboard. Falling this to
    /// false drops editor keys only. Gameplay keys are a different state.
    bool _bFeedingKeys = false;
    /// Last route decided Play owns the keyboard. Falling this to false drops
    /// gameplay keys, the same way leaving the viewport used to.
    bool _bFeedingGameKeys = false;

  public:
    [[nodiscard]] InputManager&       editorInput() { return _editorInput; }
    [[nodiscard]] const InputManager& editorInput() const { return _editorInput; }

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
    /// Compare every editor WidgetTree's cached pointer session against the
    /// platform's physical button state. Called when the platform may stop
    /// delivering a release (key focus loss, pointer left the window).
    void reconcilePointerSessionsWithPlatform(EInputCancelReason reason);
};

} // namespace ya
