// #include "OpenGLRender.h"
// #include "Core/Base.h"
// #include "Core/Log.h"
// #include "OpenGLCommandBuffer.h"
// #include "OpenGLDescriptorSet.h"
// #include "OpenGLSwapchain.h"

// #include <iostream>

// namespace ya
// {

// OpenGLRender::~OpenGLRender()
// {
//     if (m_Initialized) {
//         destroy();
//     }

//     if (_descriptorHelper) {
//         delete _descriptorHelper;
//         _descriptorHelper = nullptr;
//     }
// }

// bool OpenGLRender::init(const ya::RenderCreateInfo &ci)
// {
//     IRender::init(ci);
//     YA_PROFILE_FUNCTION();

//     bool success = initInternal(ci);
//     YA_CORE_ASSERT(success, "Failed to initialize OpenGL render!");

//     return true;
// }

// void OpenGLRender::destroy()
// {
//     destroyInternal();
// }

// bool OpenGLRender::begin(int32_t *imageIndex)
// {
//     // OpenGL doesn't need to acquire images like Vulkan
//     // Just return the current frame index
//     if (imageIndex) {
//         *imageIndex = 0; // OpenGL typically uses a single "virtual" image
//     }
//     return true;
// }

// bool OpenGLRender::end(int32_t imageIndex, std::vector<void *> CommandBuffers)
// {
//     // Execute all recorded commands (if using deferred command buffers)
//     for (auto *cmdBuf : CommandBuffers) {
//         // In OpenGL, commands are typically executed immediately
//         // but we can support deferred execution here if needed
//     }

//     // Swap buffers
//     if (_swapChain) {
// #if USE_SDL
//         if (m_Window) {
//             SDL_GL_SwapWindow(m_Window);
//         }
// #elif USE_GLFW
//         if (m_Window) {
//             glfwSwapBuffers(m_Window);
//         }
// #endif
//     }

//     return true;
// }

// void OpenGLRender::getWindowSize(int &width, int &height) const
// {
//     if (_windowProvider) {
//         _windowProvider->getWindowSize(width, height);
//     }
// }

// void OpenGLRender::setVsync(bool enabled)
// {
//     if (_swapChain) {
//         _swapChain->setVsync(enabled);
//     }
// }

// uint32_t OpenGLRender::getSwapchainWidth() const
// {
//     if (!_swapChain) return 0;
//     auto extent = _swapChain->getExtent();
//     return extent.width;
// }

// uint32_t OpenGLRender::getSwapchainHeight() const
// {
//     if (!_swapChain) return 0;
//     auto extent = _swapChain->getExtent();
//     return extent.height;
// }

// uint32_t OpenGLRender::getSwapchainImageCount() const
// {
//     // OpenGL typically uses double or triple buffering
//     // but presents as a single image to the application
//     return 1;
// }

// void OpenGLRender::allocateCommandBuffers(uint32_t count, std::vector<std::shared_ptr<ICommandBuffer>> &outBuffers)
// {
//     outBuffers.clear();
//     outBuffers.reserve(count);

//     for (uint32_t i = 0; i < count; ++i) {
//         auto cmdBuf = std::make_shared<OpenGLCommandBuffer>(this);
//         outBuffers.push_back(cmdBuf);
//         _commandBuffers.push_back(cmdBuf);
//     }
// }

// void OpenGLRender::waitIdle()
// {
//     // OpenGL doesn't have explicit device idle waiting
//     // glFinish() blocks until all commands complete
//     glFinish();
// }

// ICommandBuffer *OpenGLRender::beginIsolateCommands()
// {
//     // For immediate mode commands
//     auto *cmdBuf = new OpenGLCommandBuffer(this);
//     cmdBuf->begin(true);
//     return cmdBuf;
// }

// void OpenGLRender::endIsolateCommands(ICommandBuffer *commandBuffer)
// {
//     if (commandBuffer) {
//         commandBuffer->end();
//         // In OpenGL, commands are typically executed immediately
//         // so no need for explicit submission
//         delete commandBuffer;
//     }
// }

// IDescriptorSetHelper *OpenGLRender::getDescriptorHelper()
// {
//     if (!_descriptorHelper) {
//         _descriptorHelper = new OpenGLDescriptorHelper(this);
//     }
//     return _descriptorHelper;
// }

// bool OpenGLRender::initInternal(const RenderCreateInfo &ci)
// {
//     // Initialize window
//     initWindow(ci);
//     nativeWindow = _windowProvider->getNativeWindowPtr<void>();

// #if USE_SDL
//     m_Window = _windowProvider->getNativeWindowPtr<SDL_Window>();
// #elif USE_GLFW
//     m_Window = _windowProvider->getNativeWindowPtr<GLFWwindow>();
// #endif

//     // Create OpenGL context
//     if (!createContext()) {
//         YA_CORE_ERROR("Failed to create OpenGL context");
//         return false;
//     }

//     makeCurrent();

//     // Load OpenGL extensions
//     if (!loadGLExtensions()) {
//         YA_CORE_ERROR("Failed to load OpenGL extensions");
//         return false;
//     }

//     // Query OpenGL information
//     queryGLInfo();
//     printGLInfo();

//     // Create swapchain
//     _swapChain = new OpenGLSwapchain(this);
//     _swapChain->recreate(ci.swapchainCI);

//     m_Initialized = true;
//     return true;
// }

// void OpenGLRender::destroyInternal()
// {
//     if (!m_Initialized) {
//         return;
//     }

//     // Wait for all operations to complete
//     waitIdle();

//     // Clean up command buffers
//     _commandBuffers.clear();

//     // Clean up swapchain
//     if (_swapChain) {
//         delete _swapChain;
//         _swapChain = nullptr;
//     }

//     // Destroy context
//     destroyContext();

//     m_Initialized = false;
// }

// void OpenGLRender::initWindow(const RenderCreateInfo &ci)
// {
// #if USE_SDL
//     _windowProvider = new SDLWindowProvider();
// #elif USE_GLFW
//     _windowProvider = new GLFWWindowProvider();
// #else
//     #error "No window provider defined"
// #endif

//     _windowProvider->init();
//     _windowProvider->recreate(WindowCreateInfo{
//         .renderAPI = ci.renderAPI,
//         .width     = ci.swapchainCI.width,
//         .height    = ci.swapchainCI.height,
//     });
// }

// bool OpenGLRender::createContext()
// {
// #if USE_SDL
//     if (!m_Window) {
//         YA_CORE_ERROR("Window not initialized");
//         return false;
//     }

//     // Set OpenGL version (3.3 core profile minimum)
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

// #ifdef __APPLE__
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
// #endif

//     // Create context
//     m_GLContext = SDL_GL_CreateContext(m_Window);
//     if (!m_GLContext) {
//         YA_CORE_ERROR("Failed to create OpenGL context: {}", SDL_GetError());
//         return false;
//     }

//     // Make context current
//     if (SDL_GL_MakeCurrent(m_Window, m_GLContext) != 0) {
//         YA_CORE_ERROR("Failed to make OpenGL context current: {}", SDL_GetError());
//         return false;
//     }

// #elif USE_GLFW
//     if (!m_Window) {
//         YA_CORE_ERROR("Window not initialized");
//         return false;
//     }

//     // GLFW creates the context automatically with the window
//     glfwMakeContextCurrent(m_Window);

// #endif

//     return true;
// }

// void OpenGLRender::destroyContext()
// {
// #if USE_SDL
//     if (m_GLContext) {
//         SDL_GL_DestroyContext(m_GLContext);
//         m_GLContext = nullptr;
//     }
// #elif USE_GLFW
//     // GLFW destroys the context with the window
// #endif
// }

// bool OpenGLRender::loadGLExtensions()
// {
// #if USE_SDL
//     // Use gladLoadGLLoader with SDL's GL loader
//     if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
//         YA_CORE_ERROR("Failed to initialize GLAD");
//         return false;
//     }
// #elif USE_GLFW
//     // Use gladLoadGLLoader with GLFW's GL loader
//     if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
//         YA_CORE_ERROR("Failed to initialize GLAD");
//         return false;
//     }
// #endif

//     return true;
// }

// void OpenGLRender::queryGLInfo()
// {
//     m_RendererString = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
//     m_VersionString  = reinterpret_cast<const char *>(glGetString(GL_VERSION));
//     m_VendorString   = reinterpret_cast<const char *>(glGetString(GL_VENDOR));
// }

// void OpenGLRender::printGLInfo()
// {
//     YA_CORE_INFO("OpenGL Renderer: {}", m_RendererString);
//     YA_CORE_INFO("OpenGL Version: {}", m_VersionString);
//     YA_CORE_INFO("OpenGL Vendor: {}", m_VendorString);

//     GLint majorVersion, minorVersion;
//     glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
//     glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
//     YA_CORE_INFO("OpenGL Version (parsed): {}.{}", majorVersion, minorVersion);
// }

// void OpenGLRender::makeCurrent()
// {
// #if USE_SDL
//     if (m_Window && m_GLContext) {
//         SDL_GL_MakeCurrent(m_Window, m_GLContext);
//     }
// #elif USE_GLFW
//     if (m_Window) {
//         glfwMakeContextCurrent(m_Window);
//     }
// #endif
// }

// } // namespace ya
