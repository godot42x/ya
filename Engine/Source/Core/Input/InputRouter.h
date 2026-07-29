#pragma once

#include "Core/Api.h"
#include "Core/Event.h"
#include "Render/RenderDefines.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace ya
{

struct App;
struct InputManager;
using FInputEvent = Event;

enum class EInputCancelReason : uint8_t
{
    NodeChanged,
    CaptureReleased,
    WindowFocusLost,
    AppStateChanged,
    ModuleDetached,
};

struct FPointerCaptureRequest
{
    bool   relative   = false;
    bool   hideCursor = false;
    bool   confine    = false;
    Rect2D confinement{};
};

struct FInputReply
{
    bool                                  handled = false;
    std::optional<FPointerCaptureRequest> pointerCapture;
};

class ENGINE_API InputRouter;

struct FInputRouteContext
{
    App&         app;
    InputRouter& router;
};

struct IInputNode
{
    virtual ~IInputNode() = default;

    [[nodiscard]] virtual FInputReply route(FInputRouteContext& context, const FInputEvent& event) = 0;
    virtual void                     cancelInput(FInputRouteContext& context, EInputCancelReason reason) = 0;
};

class ENGINE_API GameInputNode final : public IInputNode
{
  private:
    InputManager* _inputManager = nullptr;

  public:
    explicit GameInputNode(InputManager& inputManager)
        : _inputManager(&inputManager)
    {
    }

    [[nodiscard]] FInputReply route(FInputRouteContext& context, const FInputEvent& event) override;
    void                     cancelInput(FInputRouteContext& context, EInputCancelReason reason) override;
};

class ENGINE_API InputRouter
{
  public:
    class FNodeRegistration
    {
      private:
        InputRouter* _owner = nullptr;
        uint64_t     _id    = 0;

      public:
        FNodeRegistration() = default;
        FNodeRegistration(InputRouter* owner, uint64_t id)
            : _owner(owner)
            , _id(id)
        {
        }

        FNodeRegistration(const FNodeRegistration&)            = delete;
        FNodeRegistration& operator=(const FNodeRegistration&) = delete;

        ENGINE_API FNodeRegistration(FNodeRegistration&& other) noexcept;
        ENGINE_API FNodeRegistration& operator=(FNodeRegistration&& other) noexcept;
        ENGINE_API ~FNodeRegistration();

        ENGINE_API void reset();
    };

  private:
    struct FNodeEntry
    {
        uint64_t    id   = 0;
        IInputNode* node = nullptr;
    };

    struct FPointerCaptureState
    {
        bool     relative   = false;
        bool     hideCursor = false;
        bool     confine    = false;
        SDL_Rect confinement{0, 0, 0, 0};

        [[nodiscard]] bool isCaptured() const
        {
            return relative || hideCursor || confine;
        }
    };

    App*                    _app         = nullptr;
    SDL_Window*             _window      = nullptr;
    IInputNode*             _defaultNode = nullptr;
    std::vector<FNodeEntry> _nodeStack;
    FPointerCaptureState    _pointerCapture;
    uint64_t                _nextNodeId  = 1;

  public:
    InputRouter() = default;

    void setApp(App& app) { _app = &app; }
    void setWindow(SDL_Window* window) { _window = window; }
    [[nodiscard]] SDL_Window* getWindow() const { return _window; }

    void setDefaultNode(IInputNode& node);
    [[nodiscard]] FNodeRegistration registerNode(IInputNode& node);

    [[nodiscard]] bool routeEvent(const FInputEvent& event);
    [[nodiscard]] bool routeUnhandledInput(const FInputEvent& event);
    void               cancelInput(EInputCancelReason reason);

    [[nodiscard]] bool isMouseCaptured() const { return _pointerCapture.isCaptured(); }

  private:
    friend class FNodeRegistration;

    void unregisterNode(uint64_t id);
    void applyReply(const FInputReply& reply);
    void applyPointerCapture(const FPointerCaptureRequest& request);
    void handleNodeTransition(IInputNode* previousNode, IInputNode* nextNode);
    [[nodiscard]] FInputRouteContext makeRouteContext();
    [[nodiscard]] IInputNode*        getActiveNode() const;
    [[nodiscard]] static SDL_Rect    toSDLRect(const Rect2D& rect);
};

} // namespace ya
