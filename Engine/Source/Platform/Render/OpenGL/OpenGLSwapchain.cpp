#include "OpenGLSwapchain.h"
#include "Core/Log.h"
#include "OpenGLRender.h"

namespace ya
{

OpenGLSwapchain::OpenGLSwapchain(OpenGLRender *render)
    : _render(render)
{
    YA_CORE_ASSERT(render, "OpenGLRender is null");

#if USE_SDL
    _window = _render->getNativeWindow<SDL_Window>();
#elif USE_GLFW
    _window = _render->getNativeWindow<GLFWwindow>();
#endif
}

bool OpenGLSwapchain::recreate(const SwapchainCreateInfo &ci)
{
    DiffInfo oldInfo{
        .extent      = _extent,
        .presentMode = _presentMode,
    };

    _ci = ci;

    // Update extent
    updateExtent();

    // Update present mode
    _presentMode  = ci.presentMode;
    _vsyncEnabled = (ci.presentMode == EPresentMode::FIFO || ci.presentMode == EPresentMode::FIFO_Relaxed);

    // Set VSync
    setVsync(_vsyncEnabled);

    // Assume RGBA8 format (most common)
    _format = EFormat::R8G8B8A8_UNORM;

    DiffInfo newInfo{
        .extent      = _extent,
        .presentMode = _presentMode,
    };

    // Fire recreate event
    bool imageRecreated = (oldInfo.extent.width != newInfo.extent.width || oldInfo.extent.height != newInfo.extent.height);
    onRecreate.broadcast(oldInfo, newInfo, imageRecreated);

    YA_CORE_TRACE("OpenGL swapchain recreated: {}x{}, VSync={}", _extent.width, _extent.height, _vsyncEnabled);
    return true;
}

void OpenGLSwapchain::setVsync(bool enabled)
{
    _vsyncEnabled = enabled;

#if USE_SDL
    if (_window) {
        // SDL3 uses SDL_GL_SetSwapInterval
        int interval = enabled ? 1 : 0;
        SDL_GL_SetSwapInterval(interval);
    }
#elif USE_GLFW
    if (_window) {
        glfwSwapInterval(enabled ? 1 : 0);
    }
#endif

    _presentMode = enabled ? EPresentMode::FIFO : EPresentMode::Immediate;
}

void OpenGLSwapchain::setPresentMode(EPresentMode::T presentMode)
{
    _presentMode = presentMode;
    bool vsync   = (presentMode == EPresentMode::FIFO || presentMode == EPresentMode::FIFO_Relaxed);
    setVsync(vsync);
}

std::vector<EPresentMode::T> OpenGLSwapchain::getAvailablePresentModes() const
{
    // OpenGL typically supports immediate and FIFO (VSync)
    return {EPresentMode::Immediate, EPresentMode::FIFO};
}

void OpenGLSwapchain::cleanup()
{
    // OpenGL doesn't need explicit swapchain cleanup
    // The window's default framebuffer is managed by the window system
}

void OpenGLSwapchain::updateExtent()
{
    if (!_window) {
        YA_CORE_ERROR("Window is null");
        return;
    }

#if USE_SDL
    int width, height;
    SDL_GetWindowSize(_window, &width, &height);
    _extent.width  = static_cast<uint32_t>(width);
    _extent.height = static_cast<uint32_t>(height);
#elif USE_GLFW
    int width, height;
    glfwGetFramebufferSize(_window, &width, &height);
    _extent.width  = static_cast<uint32_t>(width);
    _extent.height = static_cast<uint32_t>(height);
#endif
}

} // namespace ya
