#pragma once

#include "Core/Base.h"
#include "Render/Core/RenderPass.h"
#include "glad/glad.h"

namespace ya
{

struct OpenGLRender;
struct ICommandBuffer;

/**
 * @brief OpenGL render pass implementation
 * 
 * OpenGL doesn't have explicit render passes like Vulkan.
 * This class manages framebuffer binding and clear operations.
 */
struct OpenGLRenderPass : public IRenderPass
{
  private:
    OpenGLRender        *_render = nullptr;
    RenderPassCreateInfo _ci;

    // Current framebuffer (0 for default framebuffer)
    GLuint _currentFramebuffer = 0;

  public:
    OpenGLRenderPass(OpenGLRender *render);
    ~OpenGLRenderPass() override = default;

    // IRenderPass interface
    bool recreate(const RenderPassCreateInfo &ci) override;

    void begin(
        ICommandBuffer                *commandBuffer,
        void                          *framebuffer,
        const Extent2D                &extent,
        const std::vector<ClearValue> &clearValues) override;

    void end(ICommandBuffer *commandBuffer) override;

    void                                     *getHandle() const override { return (void *)(uintptr_t)_currentFramebuffer; }
    EFormat::T                                getDepthFormat() const override;
    uint32_t                                  getAttachmentCount() const override { return static_cast<uint32_t>(_ci.attachments.size()); }
    const std::vector<AttachmentDescription> &getAttachmentDesc() const override { return _ci.attachments; }
    const RenderPassCreateInfo               &getCreateInfo() const override { return _ci; }

  private:
    void clearAttachments(const std::vector<ClearValue> &clearValues);
};

} // namespace ya
