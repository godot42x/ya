#include "OpenGLCommandBuffer.h"
#include "Core/Log.h"
#include "OpenGLBuffer.h"
#include "OpenGLPipeline.h"
#include "OpenGLRender.h"

namespace ya
{

bool OpenGLCommandBuffer::begin(bool oneTimeSubmit)
{
    (void)oneTimeSubmit;
    if (_isRecording) {
        YA_CORE_WARN("OpenGLCommandBuffer::begin - already recording");
        return false;
    }

    _isRecording = true;
#if YA_CMDBUF_RECORD_MODE
    recordedCommands.clear();
#endif
    return true;
}

bool OpenGLCommandBuffer::end()
{
    if (!_isRecording) {
        YA_CORE_WARN("OpenGLCommandBuffer::end - not recording");
        return false;
    }

    _isRecording = false;
    return true;
}

void OpenGLCommandBuffer::reset()
{
#if YA_CMDBUF_RECORD_MODE
    recordedCommands.clear();
#endif
    _isRecording     = false;
    _currentPipeline = nullptr;
}

#if YA_CMDBUF_RECORD_MODE
// ========== Recording Mode: Internal execute implementations ==========

void OpenGLCommandBuffer::executeBindPipeline(IGraphicsPipeline *pipeline)
{
    if (!pipeline) {
        YA_CORE_ERROR("OpenGLCommandBuffer::bindPipeline - pipeline is null");
        return;
    }

    _currentPipeline = static_cast<OpenGLPipeline *>(pipeline);
    _currentPipeline->bind(getHandle());
}

void OpenGLCommandBuffer::executeBindVertexBuffer(uint32_t binding, const IBuffer *buffer, uint64_t offset)
{
    (void)binding;
    (void)offset;
    if (!buffer) {
        YA_CORE_ERROR("OpenGLCommandBuffer::bindVertexBuffer - buffer is null");
        return;
    }

    auto *glBuffer = static_cast<const OpenGLBuffer *>(buffer);
    glBindBuffer(GL_ARRAY_BUFFER, glBuffer->getGLBuffer());
}

void OpenGLCommandBuffer::executeBindIndexBuffer(IBuffer *buffer, uint64_t offset, bool use16BitIndices)
{
    (void)offset;
    (void)use16BitIndices;
    if (!buffer) {
        YA_CORE_ERROR("OpenGLCommandBuffer::bindIndexBuffer - buffer is null");
        return;
    }

    auto *glBuffer = static_cast<OpenGLBuffer *>(buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->getGLBuffer());
}

void OpenGLCommandBuffer::executeDraw(uint32_t vertexCount, uint32_t instanceCount,
                                      uint32_t firstVertex, uint32_t firstInstance)
{
    if (instanceCount == 1) {
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(firstVertex), static_cast<GLsizei>(vertexCount));
    }
    else {
        glDrawArraysInstancedBaseInstance(GL_TRIANGLES,
                                          static_cast<GLint>(firstVertex),
                                          static_cast<GLsizei>(vertexCount),
                                          static_cast<GLsizei>(instanceCount),
                                          static_cast<GLuint>(firstInstance));
    }
}

void OpenGLCommandBuffer::executeDrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                             uint32_t firstIndex, int32_t vertexOffset,
                                             uint32_t firstInstance)
{
    GLenum indexType = GL_UNSIGNED_INT; // Assume 32-bit indices by default

    if (instanceCount == 1) {
        glDrawElementsBaseVertex(GL_TRIANGLES,
                                 static_cast<GLsizei>(indexCount),
                                 indexType,
                                 (void *)(static_cast<uintptr_t>(firstIndex) * sizeof(uint32_t)),
                                 vertexOffset);
    }
    else {
        glDrawElementsInstancedBaseVertexBaseInstance(
            GL_TRIANGLES,
            static_cast<GLsizei>(indexCount),
            indexType,
            (void *)(static_cast<uintptr_t>(firstIndex) * sizeof(uint32_t)),
            static_cast<GLsizei>(instanceCount),
            vertexOffset,
            static_cast<GLuint>(firstInstance));
    }
}

void OpenGLCommandBuffer::executeSetViewport(float x, float y, float width, float height,
                                             float minDepth, float maxDepth)
{
    glViewport(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glDepthRangef(minDepth, maxDepth);
}

void OpenGLCommandBuffer::executeSetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

void OpenGLCommandBuffer::executeSetCullMode(ECullMode::T cullMode)
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

void OpenGLCommandBuffer::executeSetPolygonMode(EPolygonMode::T polygonMode)
{
    GLenum glMode = GL_FILL;
    switch (polygonMode) {
    case EPolygonMode::Fill:
        glMode = GL_FILL;
        break;
    case EPolygonMode::Line:
        glMode = GL_LINE;
        break;
    case EPolygonMode::Point:
        glMode = GL_POINT;
        break;
    }
    glPolygonMode(GL_FRONT_AND_BACK, glMode);
}

void OpenGLCommandBuffer::executeBindDescriptorSets(
    IPipelineLayout                        *pipelineLayout,
    uint32_t                                firstSet,
    const std::vector<DescriptorSetHandle> &descriptorSets,
    const std::vector<uint32_t>            &dynamicOffsets)
{
    (void)pipelineLayout;
    (void)firstSet;
    (void)descriptorSets;
    (void)dynamicOffsets;
    // OpenGL doesn't have descriptor sets in the same way as Vulkan
    // Uniforms and textures are bound directly to the shader program
    // This would be handled by the pipeline/material system
    YA_CORE_TRACE("OpenGLCommandBuffer::bindDescriptorSets - handled by pipeline");
}

void OpenGLCommandBuffer::executePushConstants(
    IPipelineLayout *pipelineLayout,
    EShaderStage::T  stages,
    uint32_t         offset,
    uint32_t         size,
    const void      *data)
{
    (void)pipelineLayout;
    (void)stages;
    (void)offset;
    (void)size;
    (void)data;
    // Push constants in OpenGL are implemented as uniforms
    // This would be handled by the pipeline/material system
    YA_CORE_TRACE("OpenGLCommandBuffer::pushConstants - handled by pipeline");
}

void OpenGLCommandBuffer::executeCopyBuffer(IBuffer *src, IBuffer *dst, uint64_t size,
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
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(srcOffset), static_cast<GLintptr>(dstOffset), static_cast<GLsizeiptr>(size));
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void OpenGLCommandBuffer::executeAll()
{
    for (const auto &cmd : recordedCommands) {
        std::visit(
            [&](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, RenderCommand::BindPipeline>) {
                    executeBindPipeline(arg.pipeline);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::BindVertexBuffer>) {
                    executeBindVertexBuffer(arg.binding, arg.buffer, arg.offset);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::BindIndexBuffer>) {
                    executeBindIndexBuffer(arg.buffer, arg.offset, arg.use16BitIndices);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::Draw>) {
                    executeDraw(arg.vertexCount, arg.instanceCount, arg.firstVertex, arg.firstInstance);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::DrawIndexed>) {
                    executeDrawIndexed(arg.indexCount, arg.instanceCount, arg.firstIndex, arg.vertexOffset, arg.firstInstance);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::SetViewPort>) {
                    executeSetViewport(arg.x, arg.y, arg.width, arg.height, arg.minDepth, arg.maxDepth);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::SetScissor>) {
                    executeSetScissor(arg.x, arg.y, arg.width, arg.height);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::SetCullMode>) {
                    executeSetCullMode(arg.cullMode);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::SetPolygonMode>) {
                    executeSetPolygonMode(arg.polygonMode);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::BindDescriptorSets>) {
                    executeBindDescriptorSets(arg.pipelineLayout, arg.firstSet, arg.descriptorSets, arg.dynamicOffsets);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::PushConstants>) {
                    executePushConstants(arg.pipelineLayout, arg.stages, arg.offset, static_cast<uint32_t>(arg.data.size()), arg.data.data());
                }
                else if constexpr (std::is_same_v<T, RenderCommand::CopyBuffer>) {
                    executeCopyBuffer(arg.src, arg.dst, arg.size, arg.srcOffset, arg.dstOffset);
                }
                else if constexpr (std::is_same_v<T, RenderCommand::BeginRendering> ||
                                   std::is_same_v<T, RenderCommand::EndRendering> ||
                                   std::is_same_v<T, RenderCommand::TransitionImageLayout>) {
                    (void)arg; // Not applicable for OpenGL
                }
            },
            cmd.data);
    }
    ICommandBuffer::executeAll();
}

#else
// ========== Virtual Mode: Direct GL implementations ==========

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
    (void)binding;
    (void)offset;
    if (!buffer) {
        YA_CORE_ERROR("OpenGLCommandBuffer::bindVertexBuffer - buffer is null");
        return;
    }
    auto *glBuffer = static_cast<const OpenGLBuffer *>(buffer);
    glBindBuffer(GL_ARRAY_BUFFER, glBuffer->getGLBuffer());
}

void OpenGLCommandBuffer::bindIndexBuffer(IBuffer *buffer, uint64_t offset, bool use16BitIndices)
{
    (void)offset;
    (void)use16BitIndices;
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
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(firstVertex), static_cast<GLsizei>(vertexCount));
    }
    else {
        glDrawArraysInstancedBaseInstance(GL_TRIANGLES,
                                          static_cast<GLint>(firstVertex),
                                          static_cast<GLsizei>(vertexCount),
                                          static_cast<GLsizei>(instanceCount),
                                          static_cast<GLuint>(firstInstance));
    }
}

void OpenGLCommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                      uint32_t firstIndex, int32_t vertexOffset,
                                      uint32_t firstInstance)
{
    GLenum indexType = GL_UNSIGNED_INT;
    if (instanceCount == 1) {
        glDrawElementsBaseVertex(GL_TRIANGLES,
                                 static_cast<GLsizei>(indexCount),
                                 indexType,
                                 (void *)(static_cast<uintptr_t>(firstIndex) * sizeof(uint32_t)),
                                 vertexOffset);
    }
    else {
        glDrawElementsInstancedBaseVertexBaseInstance(
            GL_TRIANGLES,
            static_cast<GLsizei>(indexCount),
            indexType,
            (void *)(static_cast<uintptr_t>(firstIndex) * sizeof(uint32_t)),
            static_cast<GLsizei>(instanceCount),
            vertexOffset,
            static_cast<GLuint>(firstInstance));
    }
}

void OpenGLCommandBuffer::setViewport(float x, float y, float width, float height,
                                      float minDepth, float maxDepth)
{
    glViewport(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glDepthRangef(minDepth, maxDepth);
}

void OpenGLCommandBuffer::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
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

void OpenGLCommandBuffer::setPolygonMode(EPolygonMode::T polygonMode)
{
    GLenum glMode = GL_FILL;
    switch (polygonMode) {
    case EPolygonMode::Fill:
        glMode = GL_FILL;
        break;
    case EPolygonMode::Line:
        glMode = GL_LINE;
        break;
    case EPolygonMode::Point:
        glMode = GL_POINT;
        break;
    }
    glPolygonMode(GL_FRONT_AND_BACK, glMode);
}

void OpenGLCommandBuffer::bindDescriptorSets(
    IPipelineLayout                        *pipelineLayout,
    uint32_t                                firstSet,
    const std::vector<DescriptorSetHandle> &descriptorSets,
    const std::vector<uint32_t>            &dynamicOffsets)
{
    (void)pipelineLayout;
    (void)firstSet;
    (void)descriptorSets;
    (void)dynamicOffsets;
    YA_CORE_TRACE("OpenGLCommandBuffer::bindDescriptorSets - handled by pipeline");
}

void OpenGLCommandBuffer::pushConstants(
    IPipelineLayout *pipelineLayout,
    EShaderStage::T  stages,
    uint32_t         offset,
    uint32_t         size,
    const void      *data)
{
    (void)pipelineLayout;
    (void)stages;
    (void)offset;
    (void)size;
    (void)data;
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
    glBindBuffer(GL_COPY_READ_BUFFER, glSrc->getGLBuffer());
    glBindBuffer(GL_COPY_WRITE_BUFFER, glDst->getGLBuffer());
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                        static_cast<GLintptr>(srcOffset),
                        static_cast<GLintptr>(dstOffset),
                        static_cast<GLsizeiptr>(size));
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void OpenGLCommandBuffer::beginRendering(const DynamicRenderingInfo &info)
{
    (void)info; // OpenGL doesn't need explicit rendering begin
}

void OpenGLCommandBuffer::endRendering()
{
    // OpenGL doesn't need explicit rendering end
}

void OpenGLCommandBuffer::transitionImageLayout(
    void                        *image,
    EImageLayout::T              oldLayout,
    EImageLayout::T              newLayout,
    const ImageSubresourceRange &subresourceRange)
{
    (void)image;
    (void)oldLayout;
    (void)newLayout;
    (void)subresourceRange;
    // OpenGL doesn't need explicit image layout transitions
}

#endif

} // namespace ya
