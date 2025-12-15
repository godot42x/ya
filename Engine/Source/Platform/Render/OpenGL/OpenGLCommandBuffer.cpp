#include "OpenGLCommandBuffer.h"
#include "Core/Log.h"
#include "OpenGLBuffer.h"
#include "OpenGLPipeline.h"
#include "OpenGLRender.h"

namespace ya
{

bool OpenGLCommandBuffer::begin(bool oneTimeSubmit)
{
    if (_isRecording) {
        YA_CORE_WARN("OpenGLCommandBuffer::begin - already recording");
        return false;
    }

    _isRecording = true;
    _commands.clear();
    return true;
}

bool OpenGLCommandBuffer::end()
{
    if (!_isRecording) {
        YA_CORE_WARN("OpenGLCommandBuffer::end - not recording");
        return false;
    }

    _isRecording = false;
    // Execute commands immediately in OpenGL
    execute();
    return true;
}

void OpenGLCommandBuffer::reset()
{
    _commands.clear();
    _isRecording    = false;
    _currentPipeline = nullptr;
}

void OpenGLCommandBuffer::bindPipeline(IGraphicsPipeline *pipeline)
{
    if (!pipeline) {
        YA_CORE_ERROR("OpenGLCommandBuffer::bindPipeline - pipeline is null");
        return;
    }

    _currentPipeline = static_cast<OpenGLPipeline *>(pipeline);
    _currentPipeline->bind(getHandle());
}

void OpenGLCommandBuffer::bindVertexBuffer(uint32_t binding, const IBuffer *buffer, uint64_t offset)
{
    if (!buffer) {
        YA_CORE_ERROR("OpenGLCommandBuffer::bindVertexBuffer - buffer is null");
        return;
    }

    auto *glBuffer = static_cast<const OpenGLBuffer *>(buffer);
    glBindBuffer(GL_ARRAY_BUFFER, glBuffer->getGLBuffer());
}

void OpenGLCommandBuffer::bindIndexBuffer(IBuffer *buffer, uint64_t offset, bool use16BitIndices)
{
    if (!buffer) {
        YA_CORE_ERROR("OpenGLCommandBuffer::bindIndexBuffer - buffer is null");
        return;
    }

    auto *glBuffer = static_cast<OpenGLBuffer *>(buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->getGLBuffer());
}

void OpenGLCommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount,
                               uint32_t firstVertex, uint32_t firstInstance)
{
    if (instanceCount == 1) {
        glDrawArrays(GL_TRIANGLES, firstVertex, vertexCount);
    }
    else {
        glDrawArraysInstancedBaseInstance(GL_TRIANGLES, firstVertex, vertexCount,
                                          instanceCount, firstInstance);
    }
}

void OpenGLCommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                      uint32_t firstIndex, int32_t vertexOffset,
                                      uint32_t firstInstance)
{
    GLenum indexType = GL_UNSIGNED_INT; // Assume 32-bit indices by default

    if (instanceCount == 1) {
        glDrawElementsBaseVertex(GL_TRIANGLES, indexCount, indexType,
                                 (void *)(firstIndex * sizeof(uint32_t)),
                                 vertexOffset);
    }
    else {
        glDrawElementsInstancedBaseVertexBaseInstance(
            GL_TRIANGLES, indexCount, indexType,
            (void *)(firstIndex * sizeof(uint32_t)),
            instanceCount, vertexOffset, firstInstance);
    }
}

void OpenGLCommandBuffer::setViewport(float x, float y, float width, float height,
                                      float minDepth, float maxDepth)
{
    glViewport(static_cast<GLint>(x), static_cast<GLint>(y),
               static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glDepthRangef(minDepth, maxDepth);
}

void OpenGLCommandBuffer::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, width, height);
}

void OpenGLCommandBuffer::setCullMode(ECullMode::T cullMode)
{
    switch (cullMode) {
    case ECullMode::None:
        glDisable(GL_CULL_FACE);
        break;
    case ECullMode::Front:
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        break;
    case ECullMode::Back:
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        break;
    case ECullMode::FrontAndBack:
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT_AND_BACK);
        break;
    }
}

void OpenGLCommandBuffer::bindDescriptorSets(
    IPipelineLayout                        *pipelineLayout,
    uint32_t                                firstSet,
    const std::vector<DescriptorSetHandle> &descriptorSets,
    const std::vector<uint32_t>            &dynamicOffsets)
{
    // OpenGL doesn't have descriptor sets in the same way as Vulkan
    // Uniforms and textures are bound directly to the shader program
    // This would be handled by the pipeline/material system
    YA_CORE_TRACE("OpenGLCommandBuffer::bindDescriptorSets - handled by pipeline");
}

void OpenGLCommandBuffer::pushConstants(
    IPipelineLayout *pipelineLayout,
    EShaderStage::T  stages,
    uint32_t         offset,
    uint32_t         size,
    const void      *data)
{
    // Push constants in OpenGL are implemented as uniforms
    // This would be handled by the pipeline/material system
    YA_CORE_TRACE("OpenGLCommandBuffer::pushConstants - handled by pipeline");
}

void OpenGLCommandBuffer::copyBuffer(IBuffer *src, IBuffer *dst, uint64_t size,
                                     uint64_t srcOffset, uint64_t dstOffset)
{
    if (!src || !dst) {
        YA_CORE_ERROR("OpenGLCommandBuffer::copyBuffer - buffer is null");
        return;
    }

    auto *glSrc = static_cast<OpenGLBuffer *>(src);
    auto *glDst = static_cast<OpenGLBuffer *>(dst);

    // Use glCopyBufferSubData for efficient GPU-side copy
    glBindBuffer(GL_COPY_READ_BUFFER, glSrc->getGLBuffer());
    glBindBuffer(GL_COPY_WRITE_BUFFER, glDst->getGLBuffer());
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                        srcOffset, dstOffset, size);
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void OpenGLCommandBuffer::execute()
{
    // In OpenGL, commands are typically executed immediately
    // This function is called at the end of recording
    // If we implement deferred command execution, we would process _commands here
}

} // namespace ya
