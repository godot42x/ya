#include "VulkanUtils.h"

#include "VulkanDevice.h"

#if USE_SDL_IMG
    #include <SDL3_image/SDL_image.h>
#elif USE_STB_IMG
    #include <stb_image.h>
#endif

void VulkanUtils::onRecreateSwapchain(struct VulkanState *state)
{
    _physicalDevice = state->m_PhysicalDevice;
    _device         = state->m_LogicalDevice;
    _commandPool    = state->m_commandPool;
    _graphicsQueue  = state->m_GraphicsQueue;
    _presentQueue   = state->m_PresentQueue;
}

uint32_t VulkanUtils::findMemoryType(VkMemoryRequirementTypeBits typeFilter, VkMemoryPropertyFlags properties)
{
    // TODO: cache?
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
    {
        if (typeFilter & (1 << i) && // vertexbuffer
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    NE_CORE_ASSERT(false, "failed to find suitable memory type!");
    return -1;
}

void VulkanUtils::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &outBuffer, VkDeviceMemory &outBufferMemory)
{
    VkBufferCreateInfo bufferInfo{
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(_device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS)
    {
        NE_CORE_ASSERT(false, "failed to create vertex buffer");
    }

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(_device, outBuffer, &memoryRequirements);

    VkMemoryAllocateInfo allocInfo{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memoryRequirements.size,
        .memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties),
    };


    if (vkAllocateMemory(_device, &allocInfo, nullptr, &outBufferMemory) != VK_SUCCESS)
    {
        NE_CORE_ASSERT(false, "failed to allocate vertex buffer memory!");
    }
    vkBindBufferMemory(_device, outBuffer, outBufferMemory, 0);
}

void VulkanUtils::createImage(uint32_t width, uint32_t height, VkFormat format,
                              VkImageTiling tiling, VkImageUsageFlags usageBits, VkMemoryPropertyFlags properties,
                              VkImage &image, VkDeviceMemory &imageMemory)
{
    VkImageCreateInfo imageInfo = {};
    {
        imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width  = static_cast<uint32_t>(width);
        imageInfo.extent.height = static_cast<uint32_t>(height);
        imageInfo.extent.depth  = 1;
        imageInfo.mipLevels     = 1; //
        imageInfo.arrayLayers   = 1;

        imageInfo.format = format;

        imageInfo.tiling = tiling; // linear/optimal ���صĲ���

        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // undefine/preinitialized gpu������ʹ�ã���һ���仯����/�ᱣ������

        imageInfo.usage = usageBits; // ������������Ŀ��/��shader�ɷ���ͼ���mesh��ɫ

        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags   = 0; // Optional
    }

    if (vkCreateImage(_device, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        NE_CORE_ASSERT(false, "failed to create image!");
    }

    VkMemoryRequirements memRequirements; // ��ѯ֧�ֵ��ڴ�����
    vkGetImageMemoryRequirements(_device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    {
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
    }
    if (vkAllocateMemory(_device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
    {
        NE_CORE_ASSERT(false, "failed to allocate image memory!");
    }

    vkBindImageMemory(_device, image, imageMemory, 0);
}

VkImageView VulkanUtils::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
{

    VkImageViewCreateInfo imageViewCreateInfo = {};
    {
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.image = image;

        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format   = format;

        // TODO: should be derive from image creation
        // or a Texture class's method
        imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // Specify target of usage of image
        imageViewCreateInfo.subresourceRange.aspectMask     = aspectFlags;
        imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
        imageViewCreateInfo.subresourceRange.levelCount     = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount     = 1;
    }

    VkImageView imageView;

    if (vkCreateImageView(_device, &imageViewCreateInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        NE_CORE_ASSERT(false, "failed to create image view!");
        return VK_NULL_HANDLE; // Return an invalid handle if creation fails
    }
    return imageView;
}

void VulkanUtils::createTextureImage(const char *path, VkImage &outImage, VkDeviceMemory &outImageMemory)
{

    int   texWidth, texHeight, texChannels;
    void *pixels;
#if USE_SDL_IMG
    // SDL_Surface *sdlSurface = IMG_Load(path);
    // pixels                  = sdlSurface->pixels;
    // texWidth                = sdlSurface->h;
    // texHeight               = sdlSurface->w;
    // texChannels             = sdlSurface->format.

#else
    pixels = stbi_load(path,
                       &texWidth,
                       &texHeight,
                       &texChannels,
                       STBI_rgb_alpha); // TODO: check the formats here!
    NE_CORE_ASSERT(pixels, "failed to load texture image! {}", path);

#endif
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    // a copy pass
    VkBuffer       gpuBuffer;
    VkDeviceMemory gpuTransferBuffer;

    createBuffer(imageSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 gpuBuffer,
                 gpuTransferBuffer);

    void *gpuData;
    vkMapMemory(_device, gpuTransferBuffer, 0, imageSize, 0, &gpuData);
    std::memcpy(gpuData, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(_device, gpuTransferBuffer);

#if USE_SDL_IMG
    // SDL_FreeSurface(sdlSurface);
#else
    stbi_image_free(pixels);
#endif


    // TODO: check image format
    createImage(texWidth,
                texHeight,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                outImage,
                outImageMemory);

    transitionImageLayout(outImage,
                          VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    copyBufferToImage(gpuBuffer,
                      outImage,
                      static_cast<uint32_t>(texWidth),
                      static_cast<uint32_t>(texHeight));

    transitionImageLayout(outImage,
                          VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(_device, gpuBuffer, nullptr);
    vkFreeMemory(_device, gpuTransferBuffer, nullptr);
}

void VulkanUtils::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferImageCopy region = {};
    {
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0; // ���ڴ��ж��
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;

        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};
    }

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    endSingleTimeCommands(commandBuffer);
}

// 转换图像布局 - 用于在不同的图像用途之间切换以优化性能
void VulkanUtils::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    // 开始一个临时的命令缓冲区来执行转换操作
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    // 创建图像内存屏障 - 用于同步图像访问和布局转换
    VkImageMemoryBarrier barrier = {};
    {
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

        // 设置旧布局和新布局
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;

        // 忽略队列族索引（不在不同队列族之间转移所有权）
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        // 指定要转换的图像
        barrier.image = image;

        // 根据新布局设置图像的方面掩码（颜色、深度、模板）
        if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            // 深度缓冲区布局 - 设置深度方面
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            // 如果格式包含模板分量，也要包含模板方面
            if (VulkanUtils::hasStencilComponent(format)) {
                barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }
        else {
            // 其他布局通常是颜色图像
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        // 设置子资源范围 - 影响整个图像的第一个mip级别和数组层
        barrier.subresourceRange.baseMipLevel   = 0; // 从第0个mip级别开始
        barrier.subresourceRange.levelCount     = 1; // 只有1个mip级别
        barrier.subresourceRange.baseArrayLayer = 0; // 从第0个数组层开始
        barrier.subresourceRange.layerCount     = 1; // 只有1个数组层
    }

    // 管道阶段标志 - 定义同步点
    VkPipelineStageFlags sourceStage;      // 源阶段（等待这个阶段完成）
    VkPipelineStageFlags destinationStage; // 目标阶段（在此阶段之前必须完成转换）

    // 情况1：未定义布局 → 传输目标布局（用于将数据复制到图像）
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;                            // 之前没有访问
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // 传输操作需要写访问

        sourceStage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; // 管道顶部（最早阶段）
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;    // 传输阶段
    }
    // 情况2：传输目标布局 → 着色器只读布局（传输完成后用于着色器采样）
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // 等待传输写入完成
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;    // 着色器需要读访问

        sourceStage      = VK_PIPELINE_STAGE_TRANSFER_BIT;        // 传输阶段
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // 片段着色器阶段
    }
    // 情况3：未定义布局 → 深度模板附件布局（用于深度测试）
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0; // 之前没有访问
        // 深度模板附件需要读写访问
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;          // 管道顶部
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; // 早期片段测试阶段
    }
    else {
        // 不支持的布局转换组合
        NE_CORE_ASSERT(false, "unsupported layout transition!");
    }

    // 在命令缓冲区中插入管道屏障来执行布局转换
    vkCmdPipelineBarrier(
        commandBuffer,    // 命令缓冲区
        sourceStage,      // 源管道阶段掩码
        destinationStage, // 目标管道阶段掩码
        0,                // 依赖标志
        0,                // 内存屏障数量
        nullptr,          // 内存屏障数组
        0,                // 缓冲区内存屏障数量
        nullptr,          // 缓冲区内存屏障数组
        1,                // 图像内存屏障数量
        &barrier);        // 图像内存屏障数组

    // 结束并提交临时命令缓冲区
    endSingleTimeCommands(commandBuffer);
}

VkCommandBuffer VulkanUtils::beginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo = {};
    {
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = _commandPool;
        allocInfo.commandBufferCount = 1;
    }

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    {
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VulkanUtils::endSingleTimeCommands(VkCommandBuffer &commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo       = {};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &commandBuffer;

    vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(_graphicsQueue);
    vkFreeCommandBuffers(_device, _commandPool, 1, &commandBuffer);
}
void VulkanUtils::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion = {};
    {
        copyRegion.srcOffset = 0; // Optional
        copyRegion.dstOffset = 0; // Optional
        copyRegion.size      = size;
    }
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(commandBuffer);
}
