#pragma once

#include "Handle.h"
#include "PlatBase.h"
#include "Render/RenderDefines.h"

namespace ya
{

struct ImageHandleTag
{};
using ImageHandle = Handle<ImageHandleTag>;

// ImageViewHandle is already defined in DescriptorSet.h
// Forward declare if needed
struct ImageViewHandleTag;
using ImageViewHandle = Handle<ImageViewHandleTag>;

struct FormatHandleTag
{};
using FormatHandle = Handle<FormatHandleTag>;



struct IImage : public plat_base<IImage>
{
    /**
     * @brief Get the platform-specific handle for this image
     * @return ImageHandle Platform handle (e.g., VkImage for Vulkan)
     */
    virtual ImageHandle getHandle() const = 0;

    virtual uint32_t   getWidth() const  = 0;
    virtual uint32_t   getHeight() const = 0;
    virtual EFormat::T getFormat() const = 0;

    virtual void setDebugName(const std::string &name) = 0;

    /**
     * @brief Get image usage flags
     */
    virtual EImageUsage::T getUsage() const = 0;
};

struct IImageView : public plat_base<IImageView>
{
    /**
     * @brief Get the platform-specific handle for this image view
     * @return ImageViewHandle Platform handle (e.g., VkImageView for Vulkan)
     */
    virtual ImageViewHandle getHandle() const = 0;

    /**
     * @brief Get the underlying image
     */
    virtual const IImage *getImage() const = 0;

    virtual void setDebugName(const std::string &name) = 0;
};

} // namespace ya
