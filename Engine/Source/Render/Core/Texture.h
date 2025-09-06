#pragma once


#include "Core/Base.h"
#include "Render/RenderDefines.h"
#include "vulkan/vulkan_core.h"



struct VulkanImage;
struct VulkanImageView;

namespace ya
{


template <typename ComponentType>
struct ColorRGBA
{
    using ty = ComponentType;
    ty r;
    ty g;
    ty b;
    ty a;
};

struct Texture
{
    EFormat::T  _format;
    uint32_t    _width    = 0;
    uint32_t    _height   = 0;
    std::string _filepath = "None";

    std::shared_ptr<VulkanImage>     image;
    std::shared_ptr<VulkanImageView> imageView;


    Texture(const std::string &filepath);
    Texture(uint32_t width, uint32_t height, const std::vector<ColorRGBA<uint8_t>> &data);

    VkImage     getVkImage();
    VkImageView getVkImageView();
    VkFormat    getVkFormat() const;

    uint32_t   getWidth() const { return _width; }
    uint32_t   getHeight() const { return _height; }
    EFormat::T getFormat() const { return _format; }

  private:

    void createImage(const void *pixels, uint32_t texWidth, uint32_t texHeight);


  private:
};
} // namespace ya