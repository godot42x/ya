#pragma once

#include "GameRuntime/InputRouter.h"

namespace ya
{

struct App;
struct EditorLayer;
struct EditorSurface;

class EditorInputNode final : public IInputNode
{
  private:
    App*           _app     = nullptr;
    EditorLayer*   _layer   = nullptr;
    EditorSurface* _surface = nullptr;

  public:
    void bind(App& app, EditorLayer& layer, EditorSurface* surface = nullptr);
    void unbind();

    [[nodiscard]] FInputReply route(FInputRouteContext& context, const FInputEvent& event) override;
    void                     cancelInput(FInputRouteContext& context, EInputCancelReason reason) override;
};

} // namespace ya
