#pragma once


#include "Core/Base.h"


struct VulkanImage;
struct VulkanImageView;

namespace ya
{

struct Texture
{

    std::shared_ptr<VulkanImage>     image;
    std::shared_ptr<VulkanImageView> imageView;


    Texture(const std::string &filepath);
};
} // namespace ya