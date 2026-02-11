#pragma once

#include "Render/Core/TextureFactory.h"

namespace ya
{

// Forward declaration
struct VulkanRender;
struct Texture;

/**
 * @brief VulkanTextureFactory - Vulkan backend texture factory implementation
 *
 * Implements ITextureFactory for Vulkan backend.
 * Provides low-level IImage/IImageView creation methods.
 * 
 * Note: High-level Texture creation is handled by Texture's static methods.
 */
class VulkanTextureFactory : public ITextureFactory
{
  public:
    VulkanTextureFactory(VulkanRender *render) : _render(render) {}
    ~VulkanTextureFactory() override = default;

    // ====== Low-level IImage/IImageView API ======

    std::shared_ptr<IImage>     createImage(const ImageCreateInfo &ci) override;
    std::shared_ptr<IImage>     createImageFromHandle(void *platformImage, EFormat::T format, EImageUsage::T usage) override;
    std::shared_ptr<IImageView> createImageView(std::shared_ptr<IImage> image, uint32_t aspectFlags) override;
    std::shared_ptr<IImageView> createImageView(std::shared_ptr<IImage> image, const ImageViewCreateInfo &ci) override;
    std::shared_ptr<IImageView> createCubeMapImageView(
        std::shared_ptr<IImage> image,
        uint32_t                aspectFlags,
        uint32_t                baseMipLevel   = 0,
        uint32_t                levelCount     = 1,
        uint32_t                baseArrayLayer = 0,
        uint32_t                layerCount     = 6) override;

    IRender *getRender() const override;
    bool     isValid() const override { return _render != nullptr; }

  private:
    VulkanRender *_render = nullptr;
};

} // namespace ya
