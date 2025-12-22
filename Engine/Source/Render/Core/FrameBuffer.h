
#pragma once

#include "Core/Base.h"
#include "Render/Core/Image.h"
#include "Render/RenderDefines.h"

namespace ya
{

// Forward declarations
struct IRender;
struct IRenderPass;

struct FrameBufferCreateInfo
{
    uint32_t                    width  = 0;
    uint32_t                    height = 0;
    std::vector<stdptr<IImage>> images; // API-specific image views (e.g., VkImageView)
};

/**
 * @brief Abstract interface for frame buffers
 * Frame buffers represent render targets with attachments
 */
struct IFrameBuffer
{
    virtual ~IFrameBuffer() = default;

    /**
     * @brief Get the native handle (e.g., VkFramebuffer for Vulkan)
     */
    virtual void *getHandle() const = 0;

    /**
     * @brief Get the handle as a specific type
     * @tparam T The type to cast to (e.g., VkFramebuffer)
     */
    template <typename T>
    T getHandleAs() const
    {
        return static_cast<T>(getHandle());
    }

    /**
     * @brief Get the extent (width, height) of this framebuffer
     */
    virtual Extent2D getExtent() const = 0;
    uint32_t         getWidth() const { return getExtent().width; }
    uint32_t         getHeight() const { return getExtent().height; }

    /**
     * @brief Create a framebuffer for the given API
     */
    static stdptr<IFrameBuffer> create(IRender *render, IRenderPass *renderPass, const FrameBufferCreateInfo &createInfo);

    virtual bool recreate(std::vector<std::shared_ptr<IImage>> images, uint32_t width, uint32_t height) = 0;

    virtual stdptr<IImageView> getImageView(uint32_t attachmentIdx) = 0;
};

} // namespace ya
