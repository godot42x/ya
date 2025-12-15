#pragma once

#include "Core/Base.h"
#include "Core/Delegate.h"
#include "Render/Core/Swapchain.h"
#include "glad/glad.h"

#if USE_SDL
    #include "SDL3/SDL.h"
#elif USE_GLFW
    #include "GLFW/glfw3.h"
#endif

namespace ya
{

struct OpenGLRender;

/**
 * @brief OpenGL swapchain implementation
 * 
 * OpenGL doesn't have explicit swapchains like Vulkan.
 * This class wraps the window's default framebuffer and swap behavior.
 */
struct OpenGLSwapchain : public ISwapchain
{
  private:
    OpenGLRender *_render = nullptr;

    // Swapchain configuration
    SwapchainCreateInfo _ci;
    Extent2D            _extent;
    EFormat::T          _format;
    EPresentMode::T     _presentMode = EPresentMode::FIFO; // VSync on by default
    bool                _vsyncEnabled = true;

#if USE_SDL
    SDL_Window *_window = nullptr;
#elif USE_GLFW
    GLFWwindow *_window = nullptr;
#endif

  public:
    OpenGLSwapchain(OpenGLRender *render);
    ~OpenGLSwapchain() override = default;

    // ISwapchain interface
    void          *getHandle() const override { return (void *)this; }
    Extent2D       getExtent() const override { return _extent; }
    EFormat::T     getFormat() const override { return _format; }
    uint32_t       getImageCount() const override { return 1; } // OpenGL uses single "virtual" image
    uint32_t       getCurImageIndex() const override { return 0; }
    bool           recreate(const SwapchainCreateInfo &ci) override;
    bool           getVsync() const override { return _vsyncEnabled; }
    void           setVsync(bool enabled) override;
    void           setPresentMode(EPresentMode::T presentMode) override;
    EPresentMode::T getPresentMode() const override { return _presentMode; }
    std::vector<EPresentMode::T> getAvailablePresentModes() const override;

    void cleanup();

  private:
    void updateExtent();
};

} // namespace ya
