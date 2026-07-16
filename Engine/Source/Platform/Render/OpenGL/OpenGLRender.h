#pragma once

#include "Core/Base.h"
#include "Core/Delegate.h"
#include "Render/Render.h"
#include "WindowProvider.h"
#include "glad/glad.h"

#include <memory>
#include <string>
#include <vector>

#if USE_SDL
    #include "SDL3/SDL.h"
#elif USE_GLFW
    #include "GLFW/glfw3.h"
#endif

namespace ya
{

// Forward declarations
struct OpenGLCommandBuffer;
struct OpenGLSwapchain;
struct OpenGLDescriptorHelper;

struct OpenGLRender : public IRender
{
  private:
    bool m_Initialized = false;

    // Context information
    std::string m_RendererString;
    std::string m_VersionString;
    std::string m_VendorString;

    // Window and context
    IWindowProvider *_windowProvider = nullptr;
    void            *nativeWindow    = nullptr;

#if USE_SDL
    SDL_GLContext m_GLContext = nullptr;
    SDL_Window   *m_Window    = nullptr;
#elif USE_GLFW
    GLFWwindow *m_Window = nullptr;
#endif

    // Swapchain
    ISwapchain *_swapChain = nullptr;

    // Descriptor helper
    OpenGLDescriptorHelper *_descriptorHelper = nullptr;

    // Command buffer pool
    std::vector<std::shared_ptr<ICommandBuffer>> _commandBuffers;

    // Frame synchronization
    uint32_t currentFrameIdx = 0;

  public:
    OpenGLRender()  = default;
    ~OpenGLRender() override;

    // IRender interface implementation
    bool init(const ya::RenderCreateInfo &ci) override;
    void destroy() override;

    bool begin(int32_t *imageIndex) override;
    bool end(int32_t imageIndex, std::vector<void *> CommandBuffers) override;

    void getWindowSize(int &width, int &height) const override;
    void setVsync(bool enabled) override;

    uint32_t getSwapchainWidth() const override;
    uint32_t getSwapchainHeight() const override;
    uint32_t getSwapchainImageCount() const override;

    void allocateCommandBuffers(uint32_t count, std::vector<std::shared_ptr<ICommandBuffer>> &outBuffers) override;
    void waitIdle() override;

    ICommandBuffer       *beginIsolateCommands() override;
    void                  endIsolateCommands(ICommandBuffer *commandBuffer) override;
    ISwapchain           *getSwapchain() override { return _swapChain; }
    IDescriptorSetHelper *getDescriptorHelper() override;

    // OpenGL-specific methods
    IWindowProvider *getWindowProvider() const { return _windowProvider; }

    template <typename T>
    T *getNativeWindow()
    {
        return static_cast<T *>(nativeWindow);
    }

  protected:
    void *getNativeWindowHandle() const override { return nativeWindow; }

  private:
    bool initInternal(const RenderCreateInfo &ci);
    void destroyInternal();

    void initWindow(const RenderCreateInfo &ci);
    bool createContext();
    void destroyContext();
    bool loadGLExtensions();
    void queryGLInfo();
    void printGLInfo();
    void makeCurrent();
};

} // namespace ya
