#pragma once


#include "Render/RenderDefines.h"
#include <memory>
#include <vector>

namespace ya
{

struct IRender;
struct ICommandBuffer;
struct IFrameBuffer;

/**
 * @brief Generic render pass interface
 */
struct IRenderPass : public plat_base<IRenderPass>
{

    ya::RenderPassCreateInfo _ci;

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
        IFrameBuffer                  *framebuffer,
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

    [[nodiscard]] uint32_t                                  getAttachmentCount() const { return static_cast<uint32_t>(_ci.attachments.size()); }
    [[nodiscard]] const std::vector<AttachmentDescription> &getAttachmentDesc() const { return _ci.attachments; }

    [[nodiscard]] std::vector<const AttachmentDescription *> getColorAttachmentDescs(uint32_t subPassIndex = 0) const
    {
        if (!isValidSubpassIndex(subPassIndex))
        {
            YA_CORE_ERROR("Invalid subpass index: {}", subPassIndex);
            return {};
        }
        std::vector<const AttachmentDescription *> colorAttachments;

        const auto &attachments = getAttachmentDesc();
        for (const auto &attachment : getSubPasses().at(subPassIndex).colorAttachments)
        {
            colorAttachments.push_back(&attachments.at(attachment.ref));
        }
        return colorAttachments;
    }
    [[nodiscard]] const AttachmentDescription *getDepthAttachmentDesc(uint32_t subPassIndex = 0) const
    {
        if (!isValidSubpassIndex(subPassIndex))
        {
            YA_CORE_ERROR("Invalid subpass index: {}", subPassIndex);
            return nullptr;
        }
        const auto &attachments = getAttachmentDesc();
        auto       &subpass     = getSubPasses().at(subPassIndex);
        if (subpass.depthAttachment.ref == -1)
        {
            return nullptr;
        }
        return &attachments.at(subpass.depthAttachment.ref);
    }

    [[nodiscard]] const RenderPassCreateInfo &getCreateInfo() const { return _ci; }

    const std::vector<RenderPassCreateInfo::SubpassInfo> &getSubPasses() const { return _ci.subpasses; }
    auto                                                  getSubPass(uint32_t index) const { return _ci.subpasses.at(index); }

    [[nodiscard]] uint32_t getSubpassCount() const { return static_cast<uint32_t>(_ci.subpasses.size()); }
    [[nodiscard]] bool     isValidSubpassIndex(uint32_t index) const { return index < getSubpassCount(); }

    bool hasDepthAttachment(uint32_t subPassIndex = 0) const
    {
        if (!isValidSubpassIndex(subPassIndex))
        {
            YA_CORE_ERROR("Invalid subpass index: {}", subPassIndex);
            return false;
        }
        auto &subpass = getSubPasses().at(subPassIndex);
        return subpass.depthAttachment.ref != -1;
    }

    /**
     * @brief Factory method to create render pass
     */
    static std::shared_ptr<IRenderPass> create(IRender *render, const RenderPassCreateInfo &ci);
};

} // namespace ya
