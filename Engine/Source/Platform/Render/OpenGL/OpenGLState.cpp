#include "OpenGLState.h"
#include "Core/Base.h"
#include <fstream>
#include <iostream>
#include <sstream>



OpenGLState::~OpenGLState()
{
    if (m_Initialized) {
        destroy();
    }
}

bool OpenGLState::initialize()
{
    if (m_Initialized) {
        return true;
    }

    // Load OpenGL extensions
    if (!loadGLExtensions()) {
        YA_CORE_ERROR("Failed to load OpenGL extensions");
        return false;
    }

    // Query OpenGL information
    queryGLInfo();

    // Print OpenGL info
    printGLInfo();

    m_Initialized = true;
    return true;
}

void OpenGLState::init(const RenderCreateInfo &renderCI)
{
    _window = new SDLWindowProvider();
    _window->init();
    _window->recreate(WindowCreateInfo{
        .renderAPI = renderCI.renderAPI,
        .width     = renderCI.swapchainCI.width,
        .height    = renderCI.swapchainCI.height,
    });
    m_Window = _window->getNativeWindowPtr<SDL_Window>();

    // EFormat::T      imageFormat = EFormat::R8G8B8A8_UNORM; // TODO: rename to surfaceFormat
    // EColorSpace::T  colorSpace  = EColorSpace::SRGB_NONLINEAR;
    // EPresentMode::T presentMode = EPresentMode::FIFO; // V-Sync by default
    // bool            bVsync      = true;               // V-Sync enabled by default

    // // Image configuration
    // uint32_t minImageCount    = 2; // Double buffering by default
    // uint32_t imageArrayLayers = 1;
    // // std::vector<EImageUsage::T> imageUsageFlags  = {EImageUsage::ColorAttachment}; // Default usage

    // // Transform and composite
    // // ESurfaceTransform::T preTransform   = ESurfaceTransform::Identity;
    // // ECompositeAlpha::T   compositeAlpha = ECompositeAlpha::Opaque;

    // // Clipping and sharing
    // bool bClipped                = true;
    // bool preferredDifferentQueue = true; // Use different queues for graphics and present if possible

    // uint32_t width  = 800;
    // uint32_t height = 600;

    // buffers

    SDL_GL_CreateContext(m_Window);
}

void OpenGLState::recreateSwapchain(const SwapchainCreateInfo &swapchainCI)
{
    // Store swapchain configuration
    m_SwapchainCI = swapchainCI;

    // Configure buffering first
    configureBuffering(swapchainCI);

    // Configure swap interval based on present mode and buffer count
    int swapInterval = 0;

    if (swapchainCI.bVsync) {
        switch (swapchainCI.presentMode) {
        case EPresentMode::FIFO:
            swapInterval = 1; // V-Sync enabled
            break;
        case EPresentMode::FIFO_Relaxed:
            swapInterval = -1; // Adaptive V-Sync (if supported)
            break;
        case EPresentMode::Mailbox:
            // For mailbox mode (triple buffering), we still use 1 but rely on driver optimization
            swapInterval = 1;
            break;
        case EPresentMode::Immediate:
        default:
            swapInterval = 0; // No V-Sync
            break;
        }
    }
    else {
        swapInterval = 0; // V-Sync disabled
    }

// Set swap interval
#if USE_SDL
    int result = SDL_GL_SetSwapInterval(swapInterval);
    if (result != 0) {
        YA_CORE_WARN("Failed to set swap interval to {}: {}", swapInterval, SDL_GetError());
        // Try fallback
        SDL_GL_SetSwapInterval(swapchainCI.bVsync ? 1 : 0);
    }
#elif USE_GLFW
    glfwSwapInterval(swapInterval);
#endif

    // Log triple buffering configuration
    if (swapchainCI.minImageCount >= 3) {
        YA_CORE_INFO("OpenGL: Triple buffering requested (minImageCount: {})", swapchainCI.minImageCount);
        if (swapchainCI.presentMode == EPresentMode::Mailbox) {
            YA_CORE_INFO("OpenGL: Using mailbox present mode for optimal triple buffering");
        }
    }
}

void OpenGLState::destroy()
{
    if (!m_Initialized) {
        return;
    }

    destroyContext();
    m_Initialized = false;
}


bool OpenGLState::createContext()
{
    UNIMPLEMENTED();
#if USE_SDL
    // SDL context creation is typically handled by the window manager
    // This is just a placeholder for context-specific setup
    return true;
#elif USE_GLFW
    // GLFW context creation is also handled by the window manager
    return true;
#else
    return false;
#endif
}

void OpenGLState::destroyContext()
{

#if USE_SDL
    if (m_GLContext) {
        SDL_GL_DestroyContext(m_GLContext);
        m_GLContext = nullptr;
    }
#elif USE_GLFW
    // GLFW context destruction is handled by window destruction
#endif
}

void OpenGLState::makeCurrent()
{
#if USE_SDL
    if (_window && m_GLContext) {
        SDL_GL_MakeCurrent(_window->getNativeWindowPtr<SDL_Window>(), m_GLContext);
    }
#elif USE_GLFW
    if (m_Window) {
        glfwMakeContextCurrent(m_Window);
    }
#endif
}

void OpenGLState::swapBuffers()
{
#if USE_SDL
    if (m_Window) {
        SDL_GL_SwapWindow(m_Window);
    }
#elif USE_GLFW
    if (m_Window) {
        glfwSwapBuffers(m_Window);
    }
#endif
}

// Triple buffering support
void OpenGLState::enableTripleBuffering()
{
    // Enable triple buffering via OpenGL context attributes
#if USE_SDL
    // Set double buffering attribute (which enables triple buffering when available)
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // Try to enable triple buffering through context creation flags
    // Note: This depends on driver support
    if (m_SwapchainCI.minImageCount >= 3) {
        YA_CORE_INFO("Attempting to enable triple buffering (3+ images requested)");
        // On some drivers, setting a higher swap interval might help with triple buffering
        if (m_SwapchainCI.presentMode == EPresentMode::Mailbox) {
            SDL_GL_SetSwapInterval(1); // Let driver decide optimal buffering
        }
    }
#elif USE_GLFW
    // GLFW doesn't have explicit triple buffering control
    // But modern drivers will automatically use triple buffering when beneficial
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
#endif
}

bool OpenGLState::isTripleBufferingSupported()
{
    // Query if triple buffering is supported
    // This is driver-dependent and not directly queryable in OpenGL
    // We can make educated guesses based on context and extensions

#if USE_SDL
    // Check if we have a valid context
    if (!m_GLContext) {
        return false;
    }

    // Modern drivers typically support triple buffering automatically
    // when using mailbox present mode or when beneficial
    const GLubyte *vendor = glGetString(GL_VENDOR);
    if (vendor) {
        std::string vendorStr = reinterpret_cast<const char *>(vendor);
        // NVIDIA, AMD, and Intel modern drivers generally support triple buffering
        if (vendorStr.find("NVIDIA") != std::string::npos ||
            vendorStr.find("AMD") != std::string::npos ||
            vendorStr.find("Intel") != std::string::npos) {
            return true;
        }
    }
#elif USE_GLFW
    // Similar logic for GLFW
    return true; // Most modern OpenGL contexts support triple buffering
#endif

    return false; // Conservative fallback
}

void OpenGLState::configureBuffering(const SwapchainCreateInfo &swapchainCI)
{
    // Configure buffering based on swapchain requirements
    if (swapchainCI.minImageCount >= 3) {
        if (isTripleBufferingSupported()) {
            enableTripleBuffering();
            YA_CORE_INFO("Triple buffering enabled (requested {} images)", swapchainCI.minImageCount);
        }
        else {
            YA_CORE_WARN("Triple buffering requested but not supported, falling back to double buffering");
        }
    }

    // Log the buffering configuration
    YA_CORE_INFO("OpenGL buffering configured:");
    YA_CORE_INFO("  - Requested images: {}", swapchainCI.minImageCount);
    YA_CORE_INFO("  - Present mode: {}", static_cast<int>(swapchainCI.presentMode));
    YA_CORE_INFO("  - VSync enabled: {}", swapchainCI.bVsync);
}

// Buffer management
GLuint OpenGLState::createBuffer(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(target, buffer);
    glBufferData(target, size, data, usage);
    checkGLError("createBuffer");
    return buffer;
}

void OpenGLState::deleteBuffer(GLuint buffer)
{
    glDeleteBuffers(1, &buffer);
    checkGLError("deleteBuffer");
}

void OpenGLState::bindBuffer(GLenum target, GLuint buffer)
{
    glBindBuffer(target, buffer);
    checkGLError("bindBuffer");
}

void OpenGLState::updateBuffer(GLuint buffer, GLenum target, GLsizeiptr size, const void *data, GLsizeiptr offset)
{
    glBindBuffer(target, buffer);
    glBufferSubData(target, offset, size, data);
    checkGLError("updateBuffer");
}

void *OpenGLState::mapBuffer(GLuint buffer, GLenum target, GLenum access)
{
    glBindBuffer(target, buffer);
    void *ptr = glMapBuffer(target, access);
    checkGLError("mapBuffer");
    return ptr;
}

void OpenGLState::unmapBuffer(GLenum target)
{
    glUnmapBuffer(target);
    checkGLError("unmapBuffer");
}

// Vertex Array Object management
GLuint OpenGLState::createVertexArray()
{
    GLuint vao;
    glGenVertexArrays(1, &vao);
    checkGLError("createVertexArray");
    return vao;
}

void OpenGLState::deleteVertexArray(GLuint vao)
{
    glDeleteVertexArrays(1, &vao);
    checkGLError("deleteVertexArray");
}

void OpenGLState::bindVertexArray(GLuint vao)
{
    glBindVertexArray(vao);
    checkGLError("bindVertexArray");
}

void OpenGLState::enableVertexAttribArray(GLuint index)
{
    glEnableVertexAttribArray(index);
    checkGLError("enableVertexAttribArray");
}

void OpenGLState::disableVertexAttribArray(GLuint index)
{
    glDisableVertexAttribArray(index);
    checkGLError("disableVertexAttribArray");
}

void OpenGLState::vertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{
    glVertexAttribPointer(index, size, type, normalized, stride, pointer);
    checkGLError("vertexAttribPointer");
}

void OpenGLState::vertexAttribDivisor(GLuint index, GLuint divisor)
{
    glVertexAttribDivisor(index, divisor);
    checkGLError("vertexAttribDivisor");
}

// Texture management
GLuint OpenGLState::createTexture(GLenum target)
{
    GLuint texture;
    glGenTextures(1, &texture);
    checkGLError("createTexture");
    return texture;
}

void OpenGLState::deleteTexture(GLuint texture)
{
    glDeleteTextures(1, &texture);
    checkGLError("deleteTexture");
}

void OpenGLState::bindTexture(GLenum target, GLuint texture)
{
    glBindTexture(target, texture);
    checkGLError("bindTexture");
}

void OpenGLState::activeTexture(GLenum texture)
{
    glActiveTexture(texture);
    checkGLError("activeTexture");
}

void OpenGLState::setupTexture2D(GLuint texture, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                                 GLenum format, GLenum type, const void *data)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, level, internalformat, width, height, 0, format, type, data);
    checkGLError("setupTexture2D");
}

void OpenGLState::generateMipmap(GLenum target)
{
    glGenerateMipmap(target);
    checkGLError("generateMipmap");
}

void OpenGLState::setTextureParameters(GLenum target, GLenum minFilter, GLenum magFilter, GLenum wrapS, GLenum wrapT)
{
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, wrapT);
    checkGLError("setTextureParameters");
}

// Shader management
GLuint OpenGLState::createShader(GLenum type, const std::string &source)
{
    GLuint      shader = glCreateShader(type);
    const char *src    = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    std::string errorLog;
    if (!checkShaderCompileStatus(shader, errorLog)) {
        YA_CORE_ERROR("Shader compilation failed: {0}", errorLog);
        glDeleteShader(shader);
        return 0;
    }

    checkGLError("createShader");
    return shader;
}

GLuint OpenGLState::createProgram(const std::vector<GLuint> &shaders)
{
    GLuint program = glCreateProgram();

    for (GLuint shader : shaders) {
        glAttachShader(program, shader);
    }

    glLinkProgram(program);

    std::string errorLog;
    if (!checkProgramLinkStatus(program, errorLog)) {
        YA_CORE_ERROR("Program linking failed: {0}", errorLog);
        glDeleteProgram(program);
        return 0;
    }

    // Detach shaders after linking
    for (GLuint shader : shaders) {
        glDetachShader(program, shader);
    }

    checkGLError("createProgram");
    return program;
}

GLuint OpenGLState::createProgramFromFiles(const std::string &vertexPath, const std::string &fragmentPath)
{
    std::string vertexSource   = readShaderSource(vertexPath);
    std::string fragmentSource = readShaderSource(fragmentPath);

    if (vertexSource.empty() || fragmentSource.empty()) {
        YA_CORE_ERROR("Failed to read shader files");
        return 0;
    }

    GLuint vertexShader   = createShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragmentShader = createShader(GL_FRAGMENT_SHADER, fragmentSource);

    if (vertexShader == 0 || fragmentShader == 0) {
        if (vertexShader)
            glDeleteShader(vertexShader);
        if (fragmentShader)
            glDeleteShader(fragmentShader);
        return 0;
    }

    GLuint program = createProgram({vertexShader, fragmentShader});

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

void OpenGLState::deleteShader(GLuint shader)
{
    glDeleteShader(shader);
    checkGLError("deleteShader");
}

void OpenGLState::deleteProgram(GLuint program)
{
    glDeleteProgram(program);
    checkGLError("deleteProgram");
}

void OpenGLState::useProgram(GLuint program)
{
    glUseProgram(program);
    checkGLError("useProgram");
}

bool OpenGLState::checkShaderCompileStatus(GLuint shader, std::string &errorLog)
{
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        GLint logLength;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        errorLog.resize(logLength);
        glGetShaderInfoLog(shader, logLength, nullptr, &errorLog[0]);
        return false;
    }

    return true;
}

bool OpenGLState::checkProgramLinkStatus(GLuint program, std::string &errorLog)
{
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (!success) {
        GLint logLength;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        errorLog.resize(logLength);
        glGetProgramInfoLog(program, logLength, nullptr, &errorLog[0]);
        return false;
    }

    return true;
}

// Framebuffer management
GLuint OpenGLState::createFramebuffer()
{
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    checkGLError("createFramebuffer");
    return fbo;
}

void OpenGLState::deleteFramebuffer(GLuint fbo)
{
    glDeleteFramebuffers(1, &fbo);
    checkGLError("deleteFramebuffer");
}

void OpenGLState::bindFramebuffer(GLenum target, GLuint fbo)
{
    glBindFramebuffer(target, fbo);
    checkGLError("bindFramebuffer");
}

void OpenGLState::framebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    glFramebufferTexture2D(target, attachment, textarget, texture, level);
    checkGLError("framebufferTexture2D");
}

bool OpenGLState::checkFramebufferStatus(GLenum target)
{
    GLenum status = glCheckFramebufferStatus(target);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        YA_CORE_ERROR("Framebuffer is not complete: {0}", status);
        return false;
    }
    return true;
}

// Renderbuffer management
GLuint OpenGLState::createRenderbuffer()
{
    GLuint rbo;
    glGenRenderbuffers(1, &rbo);
    checkGLError("createRenderbuffer");
    return rbo;
}

void OpenGLState::deleteRenderbuffer(GLuint rbo)
{
    glDeleteRenderbuffers(1, &rbo);
    checkGLError("deleteRenderbuffer");
}

void OpenGLState::bindRenderbuffer(GLuint rbo)
{
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    checkGLError("bindRenderbuffer");
}

void OpenGLState::renderbufferStorage(GLenum internalformat, GLsizei width, GLsizei height)
{
    glRenderbufferStorage(GL_RENDERBUFFER, internalformat, width, height);
    checkGLError("renderbufferStorage");
}

void OpenGLState::framebufferRenderbuffer(GLenum target, GLenum attachment, GLuint rbo)
{
    glFramebufferRenderbuffer(target, attachment, GL_RENDERBUFFER, rbo);
    checkGLError("framebufferRenderbuffer");
}

// Uniform management
GLint OpenGLState::getUniformLocation(GLuint program, const std::string &name)
{
    GLint location = glGetUniformLocation(program, name.c_str());
    checkGLError("getUniformLocation");
    return location;
}

void OpenGLState::setUniform1i(GLint location, GLint value)
{
    glUniform1i(location, value);
    checkGLError("setUniform1i");
}

void OpenGLState::setUniform1f(GLint location, GLfloat value)
{
    glUniform1f(location, value);
    checkGLError("setUniform1f");
}

void OpenGLState::setUniform3f(GLint location, GLfloat x, GLfloat y, GLfloat z)
{
    glUniform3f(location, x, y, z);
    checkGLError("setUniform3f");
}

void OpenGLState::setUniform4f(GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    glUniform4f(location, x, y, z, w);
    checkGLError("setUniform4f");
}

void OpenGLState::setUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    glUniformMatrix4fv(location, count, transpose, value);
    checkGLError("setUniformMatrix4fv");
}

// Viewport and rendering state
void OpenGLState::setViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    glViewport(x, y, width, height);
    checkGLError("setViewport");
}

void OpenGLState::setClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    glClearColor(r, g, b, a);
    checkGLError("setClearColor");
}

void OpenGLState::clear(GLbitfield mask)
{
    glClear(mask);
    checkGLError("clear");
}

void OpenGLState::enable(GLenum cap)
{
    glEnable(cap);
    checkGLError("enable");
}

void OpenGLState::disable(GLenum cap)
{
    glDisable(cap);
    checkGLError("disable");
}

void OpenGLState::setDepthFunc(GLenum func)
{
    glDepthFunc(func);
    checkGLError("setDepthFunc");
}

void OpenGLState::setBlendFunc(GLenum sfactor, GLenum dfactor)
{
    glBlendFunc(sfactor, dfactor);
    checkGLError("setBlendFunc");
}

void OpenGLState::setCullFace(GLenum mode)
{
    glCullFace(mode);
    checkGLError("setCullFace");
}

void OpenGLState::setFrontFace(GLenum mode)
{
    glFrontFace(mode);
    checkGLError("setFrontFace");
}

// Drawing
void OpenGLState::drawArrays(GLenum mode, GLint first, GLsizei count)
{
    glDrawArrays(mode, first, count);
    checkGLError("drawArrays");
}

void OpenGLState::drawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    glDrawElements(mode, count, type, indices);
    checkGLError("drawElements");
}

void OpenGLState::drawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount)
{
    glDrawArraysInstanced(mode, first, count, instancecount);
    checkGLError("drawArraysInstanced");
}

void OpenGLState::drawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount)
{
    glDrawElementsInstanced(mode, count, type, indices, instancecount);
    checkGLError("drawElementsInstanced");
}

// Error checking and debugging
void OpenGLState::checkGLError(const std::string &operation)
{
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::string errorStr = getGLErrorString(error);
        YA_CORE_ERROR("OpenGL Error in {0}: {1}", operation, errorStr);
    }
}

std::string OpenGLState::getGLErrorString(GLenum error)
{
    switch (error) {
        CASE_ENUM_TO_STR(GL_NO_ERROR);
        CASE_ENUM_TO_STR(GL_INVALID_ENUM);
        CASE_ENUM_TO_STR(GL_INVALID_VALUE);
        CASE_ENUM_TO_STR(GL_INVALID_OPERATION);
        CASE_ENUM_TO_STR(GL_OUT_OF_MEMORY);
        CASE_ENUM_TO_STR(GL_INVALID_FRAMEBUFFER_OPERATION);
    default:
        return std::format("Unknown Error: {}", (int)error);
    }
}

void OpenGLState::printGLInfo()
{
    YA_CORE_INFO("OpenGL Renderer: {0}", m_RendererString);
    YA_CORE_INFO("OpenGL Version: {0}", m_VersionString);
    YA_CORE_INFO("OpenGL Vendor: {0}", m_VendorString);
}

// Private helper functions
bool OpenGLState::loadGLExtensions()
{
#if USE_SDL
    int ret = gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
    return ret != 0;
#elif USE_GLFW
    int ret = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    return ret != 0;
#else
    return false;
#endif
}

void OpenGLState::queryGLInfo()
{
    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *version  = glGetString(GL_VERSION);
    const GLubyte *vendor   = glGetString(GL_VENDOR);


    if (renderer)
        m_RendererString = reinterpret_cast<const char *>(renderer);
    if (version)
        m_VersionString = reinterpret_cast<const char *>(version);
    if (vendor)
        m_VendorString = reinterpret_cast<const char *>(vendor);
}

std::string OpenGLState::readShaderSource(const std::string &filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open()) {
        YA_CORE_ERROR("Failed to open shader file: {0}", filepath);
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
