#include "Buffer.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Render.h"


namespace ya
{

std::shared_ptr<IBuffer> IBuffer::create(IRender *render, const BufferCreateInfo &ci)
{
    if (auto *vkRender = dynamic_cast<VulkanRender *>(render))
    {
        // VulkanBuffer will handle the conversion from generic to Vulkan-specific types
        return VulkanBuffer::create(vkRender, ci);
    }

    return nullptr;
}

} // namespace ya
