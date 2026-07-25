#pragma once

#include "Core/Event.h"
#include "Render/RenderDefines.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace ya
{

struct InputManager;
using FInputEvent = Event;

enum class EInputCancelReason : uint8_t
{
    RootChanged,
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
    bool                                 handled = false;
    std::optional<FPointerCaptureRequest> pointerCapture;
};

struct IHostInputRoot
{
    virtual ~IHostInputRoot() = default;

    [[nodiscard]] virtual FInputReply route(const FInputEvent& event) = 0;
    virtual void                     cancelInput(EInputCancelReason reason) = 0;
};

class GameInputRoot final : public IHostInputRoot
{
  private:
    InputManager* _inputManager = nullptr;

  public:
    explicit GameInputRoot(InputManager& inputManager)
        : _inputManager(&inputManager)
    {
    }

    [[nodiscard]] FInputReply route(const FInputEvent& event) override;
    void                     cancelInput(EInputCancelReason reason) override;
};

class InputRouter
{
  public:
    class FRootRegistration
    {
      private:
        InputRouter* _owner = nullptr;
        uint64_t     _id    = 0;

      public:
        FRootRegistration() = default;
        FRootRegistration(InputRouter* owner, uint64_t id)
            : _owner(owner)
            , _id(id)
        {
        }

        FRootRegistration(const FRootRegistration&)            = delete;
        FRootRegistration& operator=(const FRootRegistration&) = delete;

        FRootRegistration(FRootRegistration&& other) noexcept;
        FRootRegistration& operator=(FRootRegistration&& other) noexcept;
        ~FRootRegistration();

        void reset();
    };

  private:
    struct FRootEntry
    {
        uint64_t        id   = 0;
        IHostInputRoot* root = nullptr;
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

    SDL_Window*              _window         = nullptr;
    IHostInputRoot*          _defaultRoot    = nullptr;
    std::vector<FRootEntry>  _rootStack;
    FPointerCaptureState     _pointerCapture;
    uint64_t                 _nextRootId     = 1;

  public:
    InputRouter() = default;

    void setWindow(SDL_Window* window) { _window = window; }
    [[nodiscard]] SDL_Window* getWindow() const { return _window; }

    void setDefaultRoot(IHostInputRoot& root);
    [[nodiscard]] FRootRegistration registerRoot(IHostInputRoot& root);

    [[nodiscard]] bool routeEvent(const FInputEvent& event);
    void               cancelInput(EInputCancelReason reason);

    [[nodiscard]] bool isMouseCaptured() const { return _pointerCapture.isCaptured(); }

  private:
    friend class FRootRegistration;

    void unregisterRoot(uint64_t id);
    void applyReply(const FInputReply& reply);
    void applyPointerCapture(const FPointerCaptureRequest& request);
    void handleRootTransition(IHostInputRoot* previousRoot, IHostInputRoot* nextRoot);
    [[nodiscard]] IHostInputRoot* getActiveRoot() const;
    [[nodiscard]] static SDL_Rect toSDLRect(const Rect2D& rect);
};

} // namespace ya
