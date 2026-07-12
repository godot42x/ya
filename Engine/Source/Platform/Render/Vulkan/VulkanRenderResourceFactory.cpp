#include "VulkanRenderResourceFactory.h"

#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Platform/Render/Vulkan/VulkanSampler.h"

namespace ya
{

std::shared_ptr<IBuffer> VulkanRenderResourceFactory::createBuffer(const BufferCreateInfo& desc)
{
    return VulkanBuffer::create(_render, desc);
}

std::shared_ptr<Sampler> VulkanRenderResourceFactory::createSampler(const SamplerDesc& desc)
{
    return makeShared<VulkanSampler>(_render, desc);
}

} // namespace ya
