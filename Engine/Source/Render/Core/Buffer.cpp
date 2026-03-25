#include "Buffer.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Render.h"


namespace ya
{

std::shared_ptr<IBuffer> IBuffer::create(IRender* render, const BufferCreateInfo& ci)
{
    if (render->getAPI() == ERenderAPI::Vulkan) {
        {
            // VulkanBuffer will handle the conversion from generic to Vulkan-specific types
            return VulkanBuffer::create(render->as<VulkanRender>(), ci);
        }

        return nullptr;
    }
}

} // namespace ya