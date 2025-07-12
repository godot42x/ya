#include "VulkanResourceManager.h"
#include "Core/Log.h"
void VulkanResourceManager::initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice,
                                       VkCommandPool commandPool, VkQueue graphicsQueue)
{
    m_logicalDevice  = logicalDevice;
    m_physicalDevice = physicalDevice;
    m_commandPool    = commandPool;
    m_graphicsQueue  = graphicsQueue;
}

void VulkanResourceManager::cleanup()
{
    // Destroy all samplers
    for (auto &pair : m_samplers) {
        vkDestroySampler(m_logicalDevice, pair.second, nullptr);
    }
    m_samplers.clear();
}



VkSampler VulkanResourceManager::getOrCreateSampler(SamplerType samplerType, VkSamplerCreateInfo *ci)
{

    auto it = m_samplers.find(samplerType);
    if (it != m_samplers.end()) {
        return it->second; // Return existing sampler
    }

    // Create new sampler
    NE_CORE_ASSERT(ci != nullptr, "SamplerCreateInfo must not be null!");
    VkSampler sampler;
    VkResult  result = vkCreateSampler(m_logicalDevice, ci, nullptr, &sampler);
    NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create sampler!");
    m_samplers[samplerType] = sampler; // Store the new sampler
    return sampler;
}
