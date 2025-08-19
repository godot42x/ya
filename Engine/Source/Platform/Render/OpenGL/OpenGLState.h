#pragma once

#include "Core/Log.h"

#include "Render/Render.h"
#include "WindowProvider.h"
#include "glad/glad.h"
#include <string>
#include <unordered_map>
#include <vector>


#if USE_SDL
    #include "SDL3/SDL.h"
#elif USE_GLFW
    #include "GLFW/glfw3.h"
#endif


#define DEBUG_GL_STATE 1


class OpenGLState
{

  private:
    bool m_Initialized = false;

    // Context information
    std::string m_RendererString;
    std::string m_VersionString;
    std::string m_VendorString;

    // Swapchain configuration
    SwapchainCreateInfo m_SwapchainCI;

    IWindowProvider *_window = nullptr;

    // Platform-specific context
#if USE_SDL
    SDL_GLContext m_GLContext = nullptr;
    SDL_Window   *m_Window    = nullptr;
#elif USE_GLFW
    GLFWwindow *m_Window = nullptr;
#endif

  public:
  public:
    OpenGLState() = default;
    ~OpenGLState();

    // Core initialization and cleanup
    bool initialize();
    void destroy();

    void init(const RenderCreateInfo &renderCI);
    void recreateSwapchain(const SwapchainCreateInfo &swapchainCI);
    void recreateRenderPass(const RenderPassCreateInfo &renderPassCI) {};


    struct FrameBufferCreateInfo
    {
    };

    void recreateFramebuffer(const FrameBufferCreateInfo &framebufferCI) {};
    void recreatePipeline(const GraphicsPipelineCreateInfo &pipelineCI) {};

    // Context management
    bool createContext();
    void destroyContext();
    void makeCurrent();
    void swapBuffers();

    // Triple buffering support
    void enableTripleBuffering();
    bool isTripleBufferingSupported();
    void configureBuffering(const SwapchainCreateInfo &swapchainCI);


    // Buffer management
    GLuint createBuffer(GLenum target, GLsizeiptr size, const void *data, GLenum usage = GL_STATIC_DRAW);
    void   deleteBuffer(GLuint buffer);
    void   bindBuffer(GLenum target, GLuint buffer);
    void   updateBuffer(GLuint buffer, GLenum target, GLsizeiptr size, const void *data, GLsizeiptr offset = 0);
    void  *mapBuffer(GLuint buffer, GLenum target, GLenum access);
    void   unmapBuffer(GLenum target);

    // Vertex Array Object management
    GLuint createVertexArray();
    void   deleteVertexArray(GLuint vao);
    void   bindVertexArray(GLuint vao);
    void   enableVertexAttribArray(GLuint index);
    void   disableVertexAttribArray(GLuint index);
    void   vertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
    void   vertexAttribDivisor(GLuint index, GLuint divisor);

    // Texture management
    GLuint createTexture(GLenum target);
    void   deleteTexture(GLuint texture);
    void   bindTexture(GLenum target, GLuint texture);
    void   activeTexture(GLenum texture);
    void   setupTexture2D(GLuint texture, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                          GLenum format, GLenum type, const void *data);
    void   generateMipmap(GLenum target);
    void   setTextureParameters(GLenum target, GLenum minFilter = GL_LINEAR, GLenum magFilter = GL_LINEAR,
                                GLenum wrapS = GL_REPEAT, GLenum wrapT = GL_REPEAT);

    // Shader management
    GLuint createShader(GLenum type, const std::string &source);
    GLuint createProgram(const std::vector<GLuint> &shaders);
    GLuint createProgramFromFiles(const std::string &vertexPath, const std::string &fragmentPath);
    void   deleteShader(GLuint shader);
    void   deleteProgram(GLuint program);
    void   useProgram(GLuint program);
    bool   checkShaderCompileStatus(GLuint shader, std::string &errorLog);
    bool   checkProgramLinkStatus(GLuint program, std::string &errorLog);

    // Framebuffer management
    GLuint createFramebuffer();
    void   deleteFramebuffer(GLuint fbo);
    void   bindFramebuffer(GLenum target, GLuint fbo);
    void   framebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
    bool   checkFramebufferStatus(GLenum target);

    // Renderbuffer management
    GLuint createRenderbuffer();
    void   deleteRenderbuffer(GLuint rbo);
    void   bindRenderbuffer(GLuint rbo);
    void   renderbufferStorage(GLenum internalformat, GLsizei width, GLsizei height);
    void   framebufferRenderbuffer(GLenum target, GLenum attachment, GLuint rbo);

    // Uniform management
    GLint getUniformLocation(GLuint program, const std::string &name);
    void  setUniform1i(GLint location, GLint value);
    void  setUniform1f(GLint location, GLfloat value);
    void  setUniform3f(GLint location, GLfloat x, GLfloat y, GLfloat z);
    void  setUniform4f(GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
    void  setUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

    // Viewport and rendering state
    void setViewport(GLint x, GLint y, GLsizei width, GLsizei height);
    void setClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
    void clear(GLbitfield mask);
    void enable(GLenum cap);
    void disable(GLenum cap);
    void setDepthFunc(GLenum func);
    void setBlendFunc(GLenum sfactor, GLenum dfactor);
    void setCullFace(GLenum mode);
    void setFrontFace(GLenum mode);

    // Drawing
    void drawArrays(GLenum mode, GLint first, GLsizei count);
    void drawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
    void drawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);
    void drawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount);

    // Error checking and debugging
    void        checkGLError(const std::string &operation = "");
    std::string getGLErrorString(GLenum error);
    void        printGLInfo();

    // State queries
    bool               isInitialized() const { return m_Initialized; }
    const std::string &getRenderer() const { return m_RendererString; }
    const std::string &getVersion() const { return m_VersionString; }
    const std::string &getVendor() const { return m_VendorString; }


    // Internal helper functions
    bool        loadGLExtensions();
    void        queryGLInfo();
    std::string readShaderSource(const std::string &filepath);
};
