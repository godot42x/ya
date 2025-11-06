#pragma once

#include "PlatBase.h"
#include "Render/RenderDefines.h"
#include <memory>
#include <vector>

namespace ya
{

struct IRender;
struct ICommandBuffer;

/**
 * @brief Generic render pass interface
 */
struct IRenderPass : public plat_base<IRenderPass>
{
  public:
    virtual ~IRenderPass() = default;

    IRenderPass()                               = default;
    IRenderPass(const IRenderPass &)            = delete;
    IRenderPass &operator=(const IRenderPass &) = delete;
    IRenderPass(IRenderPass &&)                 = default;
    IRenderPass &operator=(IRenderPass &&)      = default;


    /**
     * @brief Recreate the render pass with new configuration
     */
    virtual bool recreate(const RenderPassCreateInfo &ci) = 0;

    /**
     * @brief Begin the render pass
     * @param commandBuffer Command buffer to record commands
     * @param framebuffer Framebuffer handle (backend-specific)
     * @param extent Render area extent
     * @param clearValues Clear values for attachments
     */
    virtual void begin(
        ICommandBuffer                *commandBuffer,
        void                          *framebuffer,
        const Extent2D                &extent,
        const std::vector<ClearValue> &clearValues) = 0;

    /**
     * @brief End the render pass
     */
    virtual void end(ICommandBuffer *commandBuffer) = 0;

    /**
     * @brief Get the native handle (backend-specific)
     */
    virtual void *getHandle() const = 0;

    /**
     * @brief Get typed native handle
     */
    template <typename T>
    T getHandleAs() const { return static_cast<T>(getHandle()); }

    /**
     * @brief Get the depth format used by this render pass
     */
    virtual EFormat::T getDepthFormat() const = 0;

    /**
     * @brief Get attachment count
     */
    virtual uint32_t getAttachmentCount() const = 0;

    /**
     * @brief Get attachments
     */
    virtual const std::vector<AttachmentDescription> &getAttachments() const = 0;

    /**
     * @brief Get the creation info
     */
    virtual const RenderPassCreateInfo &getCreateInfo() const = 0;

    /**
     * @brief Factory method to create render pass
     */
    static std::shared_ptr<IRenderPass> create(IRender *render);
};

} // namespace ya
