#pragma once

#include "Core/Input/InputRouter.h"

namespace ya
{

struct App;
struct EditorLayer;

class EditorInputNode final : public IInputNode
{
  private:
    App*         _app   = nullptr;
    EditorLayer* _layer = nullptr;

  public:
    void bind(App& app, EditorLayer& layer);
    void unbind();

    [[nodiscard]] FInputReply route(FInputRouteContext& context, const FInputEvent& event) override;
    void                     cancelInput(FInputRouteContext& context, EInputCancelReason reason) override;
};

} // namespace ya
