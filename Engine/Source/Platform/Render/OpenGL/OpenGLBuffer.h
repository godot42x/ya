#pragma once

#include "Core/Base.h"
#include "Render/Core/Buffer.h"
#include "glad/glad.h"

#include <memory>
#include <string>

namespace ya
{

struct OpenGLRender;

struct OpenGLBuffer : public ya::IBuffer
{
  private:
    OpenGLRender *_render = nullptr;

    std::string  name         = "None";
    GLuint       _handle      = 0;
    GLenum       _target      = GL_ARRAY_BUFFER;
    GLsizeiptr   _size        = 0;
    EBufferUsage _usageFlags  = EBufferUsage::None;
    bool         bHostVisible = false;
    void        *_mappedData  = nullptr;

    // Helper to convert usage flags to GL usage
    static GLenum getGLUsage(EBufferUsage usage, EMemoryProperty memProps);
    static GLenum getGLTarget(EBufferUsage usage);

  public:
    OpenGLBuffer(OpenGLRender *render, const BufferCreateInfo &ci);
    ~OpenGLBuffer() override;

    // Delete copy
    OpenGLBuffer(const OpenGLBuffer &)            = delete;
    OpenGLBuffer &operator=(const OpenGLBuffer &) = delete;

    // Enable move
    OpenGLBuffer(OpenGLBuffer &&)            = default;
    OpenGLBuffer &operator=(OpenGLBuffer &&) = default;

    // IBuffer interface implementation
    bool               writeData(const void *data, uint32_t size = 0, uint32_t offset = 0) override;
    bool               flush(uint32_t size = 0, uint32_t offset = 0) override;
    void               unmap() override;
    BufferHandle       getHandle() const override { return BufferHandle((void *)(uintptr_t)_handle); }
    uint32_t           getSize() const override { return static_cast<uint32_t>(_size); }
    bool               isHostVisible() const override { return bHostVisible; }
    const std::string &getName() const override { return name; }

    // OpenGL-specific methods
    GLuint getGLBuffer() const { return _handle; }
    GLenum getTarget() const { return _target; }

    void bind() const;
    void unbind() const;

    // Factory method
    static std::shared_ptr<OpenGLBuffer> create(OpenGLRender *render, const BufferCreateInfo &ci)
    {
        return std::make_shared<OpenGLBuffer>(render, ci);
    }

  protected:
    void mapInternal(void **ptr) override;

  private:
    void createBufferInternal(const void *data, GLsizeiptr size, GLenum usage);
};

} // namespace ya
