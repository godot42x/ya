#pragma once

#include "Core/Api.h"
#include "Core/Common/Types.h"
#include "Core/Event.h"
#include "Core/Input/Cursor.h"
#include "Core/Input/InputMode.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ya
{

struct App;
struct InputManager;
struct INativeWindow;
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

class YA_GAME_RUNTIME_API InputRouter;

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
    /// Cursor requested by this node after the last routed event. `nullopt`
    /// means the router should fall back to GameUIHost hover (game-only UI).
    [[nodiscard]] virtual std::optional<ECursorType> getCursor() const;
};

class YA_GAME_RUNTIME_API GameInputNode final : public IInputNode
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

class YA_GAME_RUNTIME_API InputRouter
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

        YA_GAME_RUNTIME_API FNodeRegistration(FNodeRegistration&& other) noexcept;
        YA_GAME_RUNTIME_API FNodeRegistration& operator=(FNodeRegistration&& other) noexcept;
        YA_GAME_RUNTIME_API ~FNodeRegistration();

        YA_GAME_RUNTIME_API void reset();
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
        Rect2D   confinement{};

        [[nodiscard]] bool isCaptured() const
        {
            return relative || hideCursor || confine;
        }
    };

    App*                    _app         = nullptr;
    INativeWindow*          _window      = nullptr;
    IInputNode*             _defaultNode = nullptr;
    std::vector<FNodeEntry> _nodeStack;
    FPointerCaptureState    _pointerCapture;
    uint64_t                _nextNodeId  = 1;

  public:
    InputRouter() = default;
    ~InputRouter() = default;

    void setApp(App& app) { _app = &app; }
    void setWindow(INativeWindow* window) { _window = window; }
    [[nodiscard]] INativeWindow* getWindow() const { return _window; }

    void setDefaultNode(IInputNode& node);
    [[nodiscard]] FNodeRegistration registerNode(IInputNode& node);

    [[nodiscard]] bool routeEvent(const FInputEvent& event);
    [[nodiscard]] bool routeUnhandledInput(const FInputEvent& event);
    void               cancelInput(EInputCancelReason reason);
    /// Apply the cursor baseline of an input-mode switch: releases any active
    /// game capture, then shows/hides the cursor per mode.
    void applyInputMode(EInputMode mode);

    [[nodiscard]] bool isMouseCaptured() const { return _pointerCapture.isCaptured(); }

  private:
    friend class FNodeRegistration;

    void unregisterNode(uint64_t id);
    void applyReply(const FInputReply& reply);
    void applyPointerCapture(const FPointerCaptureRequest& request);
    void handleNodeTransition(IInputNode* previousNode, IInputNode* nextNode);
    void updateCursor();
    [[nodiscard]] FInputRouteContext makeRouteContext();
    [[nodiscard]] IInputNode*        getActiveNode() const;
};

} // namespace ya
