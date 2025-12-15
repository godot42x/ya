// #include "OpenGLRenderPass.h"
// #include "Core/Log.h"
// #include "OpenGLRender.h"

// namespace ya
// {

// OpenGLRenderPass::OpenGLRenderPass(OpenGLRender *render)
//     : _render(render)
// {
//     YA_CORE_ASSERT(render, "OpenGLRender is null");
// }

// bool OpenGLRenderPass::recreate(const RenderPassCreateInfo &ci)
// {
//     _ci = ci;
//     // OpenGL doesn't need explicit render pass creation
//     return true;
// }

// void OpenGLRenderPass::begin(
//     ICommandBuffer                *commandBuffer,
//     void                          *framebuffer,
//     const Extent2D                &extent,
//     const std::vector<ClearValue> &clearValues)
// {
//     // Bind framebuffer
//     _currentFramebuffer = framebuffer ? *static_cast<GLuint *>(framebuffer) : 0;
//     glBindFramebuffer(GL_FRAMEBUFFER, _currentFramebuffer);

//     // Set viewport
//     glViewport(0, 0, extent.width, extent.height);

//     // Clear attachments
//     clearAttachments(clearValues);
// }

// void OpenGLRenderPass::end(ICommandBuffer *commandBuffer)
// {
//     // Unbind framebuffer (optional, but good practice)
//     glBindFramebuffer(GL_FRAMEBUFFER, 0);
//     _currentFramebuffer = 0;
// }

// EFormat::T OpenGLRenderPass::getDepthFormat() const
// {
//     // Find depth attachment
//     for (const auto &attachment : _ci.attachments) {
//         // Check if this is a depth format
//         if (attachment.format == EFormat::D32_SFLOAT ||
//             attachment.format == EFormat::D24_UNORM_S8_UINT ||
//             attachment.format == EFormat::D16_UNORM) {
//             return attachment.format;
//         }
//     }

//     return EFormat::Undefined;
// }

// void OpenGLRenderPass::clearAttachments(const std::vector<ClearValue> &clearValues)
// {
//     if (clearValues.empty()) {
//         return;
//     }

//     GLbitfield clearMask = 0;
//     size_t     colorIndex = 0;

//     for (size_t i = 0; i < _ci.attachments.size() && i < clearValues.size(); ++i) {
//         const auto &attachment = _ci.attachments[i];
//         const auto &clearValue = clearValues[i];

//         // Determine if this is a color or depth/stencil attachment
//         bool isDepth   = (attachment.format == EFormat::D32_SFLOAT ||
//                         attachment.format == EFormat::D24_UNORM_S8_UINT ||
//                         attachment.format == EFormat::D16_UNORM);
//         bool isStencil = (attachment.format == EFormat::D24_UNORM_S8_UINT ||
//                           attachment.format == EFormat::S8_UINT);

//         if (isDepth) {
//             glClearDepthf(clearValue.depthStencil.depth);
//             clearMask |= GL_DEPTH_BUFFER_BIT;
//         }

//         if (isStencil) {
//             glClearStencil(static_cast<GLint>(clearValue.depthStencil.stencil));
//             clearMask |= GL_STENCIL_BUFFER_BIT;
//         }

//         if (!isDepth && !isStencil) {
//             // Color attachment
//             if (colorIndex == 0) {
//                 glClearColor(clearValue.color.float32[0],
//                              clearValue.color.float32[1],
//                              clearValue.color.float32[2],
//                              clearValue.color.float32[3]);
//                 clearMask |= GL_COLOR_BUFFER_BIT;
//             }
//             colorIndex++;
//         }
//     }

//     // Perform the clear
//     if (clearMask != 0) {
//         glClear(clearMask);
//     }
// }

// } // namespace ya
