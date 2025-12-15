#include "OpenGLBuffer.h"
#include "Core/Log.h"
#include "OpenGLRender.h"

namespace ya
{

GLenum OpenGLBuffer::getGLUsage(EBufferUsage usage, EMemoryProperty memProps)
{
    // Determine GL usage hint based on memory properties
    bool hostVisible = static_cast<uint32_t>(memProps & EMemoryProperty::HostVisible) != 0;
    bool deviceLocal = static_cast<uint32_t>(memProps & EMemoryProperty::DeviceLocal) != 0;

    if (hostVisible && !deviceLocal) {
        // CPU-writable, frequently updated
        return GL_DYNAMIC_DRAW;
    }
    else if (hostVisible && deviceLocal) {
        // CPU-writable, GPU-optimal
        return GL_STREAM_DRAW;
    }
    else if (deviceLocal) {
        // GPU-only, static data
        return GL_STATIC_DRAW;
    }

    return GL_STATIC_DRAW;
}

GLenum OpenGLBuffer::getGLTarget(EBufferUsage usage)
{
    if (static_cast<uint32_t>(usage & EBufferUsage::VertexBuffer)) {
        return GL_ARRAY_BUFFER;
    }
    else if (static_cast<uint32_t>(usage & EBufferUsage::IndexBuffer)) {
        return GL_ELEMENT_ARRAY_BUFFER;
    }
    else if (static_cast<uint32_t>(usage & EBufferUsage::UniformBuffer)) {
        return GL_UNIFORM_BUFFER;
    }
    else if (static_cast<uint32_t>(usage & EBufferUsage::StorageBuffer)) {
        return GL_SHADER_STORAGE_BUFFER;
    }

    return GL_ARRAY_BUFFER;
}

OpenGLBuffer::OpenGLBuffer(OpenGLRender *render, const BufferCreateInfo &ci)
    : _render(render), name(ci.label), _usageFlags(ci.usage), _size(ci.size)
{
    YA_CORE_ASSERT(render, "OpenGLRender is null");

    _target      = getGLTarget(ci.usage);
    bHostVisible = static_cast<uint32_t>(ci.memProperties & EMemoryProperty::HostVisible) != 0;

    GLenum glUsage = getGLUsage(ci.usage, ci.memProperties);

    if (ci.data.has_value()) {
        createBufferInternal(ci.data.value(), ci.size, glUsage);
    }
    else {
        createBufferInternal(nullptr, ci.size, glUsage);
    }

    YA_CORE_TRACE("Created OpenGLBuffer [{}]: {} of size: {}", name, _handle, _size);
}

OpenGLBuffer::~OpenGLBuffer()
{
    if (_mappedData) {
        unmap();
    }

    if (_handle != 0) {
        glDeleteBuffers(1, &_handle);
        _handle = 0;
    }
}

void OpenGLBuffer::createBufferInternal(const void *data, GLsizeiptr size, GLenum usage)
{
    glGenBuffers(1, &_handle);
    glBindBuffer(_target, _handle);
    glBufferData(_target, size, data, usage);
    glBindBuffer(_target, 0);
}

bool OpenGLBuffer::writeData(const void *data, uint32_t size, uint32_t offset)
{
    if (!data) {
        YA_CORE_ERROR("OpenGLBuffer::writeData - data is null");
        return false;
    }

    uint32_t writeSize = (size == 0) ? _size : size;
    if (offset + writeSize > _size) {
        YA_CORE_ERROR("OpenGLBuffer::writeData - write would exceed buffer size");
        return false;
    }

    glBindBuffer(_target, _handle);
    glBufferSubData(_target, offset, writeSize, data);
    glBindBuffer(_target, 0);

    return true;
}

bool OpenGLBuffer::flush(uint32_t size, uint32_t offset)
{
    // OpenGL doesn't require explicit flushing for coherent memory
    // This is a no-op for OpenGL
    return true;
}

void OpenGLBuffer::mapInternal(void **ptr)
{
    if (!bHostVisible) {
        YA_CORE_ERROR("OpenGLBuffer::map - buffer is not host visible");
        *ptr = nullptr;
        return;
    }

    if (_mappedData) {
        *ptr = _mappedData;
        return;
    }

    glBindBuffer(_target, _handle);
    _mappedData = glMapBuffer(_target, GL_READ_WRITE);
    glBindBuffer(_target, 0);

    *ptr = _mappedData;
}

void OpenGLBuffer::unmap()
{
    if (!_mappedData) {
        return;
    }

    glBindBuffer(_target, _handle);
    glUnmapBuffer(_target);
    glBindBuffer(_target, 0);

    _mappedData = nullptr;
}

void OpenGLBuffer::bind() const
{
    glBindBuffer(_target, _handle);
}

void OpenGLBuffer::unbind() const
{
    glBindBuffer(_target, 0);
}

} // namespace ya
