#pragma once

#include "Core/Log.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>


class VulkanResourceManager
{
  public:
    enum class SamplerType
    {
        Default = 0,


    };

  private:
    VkDevice         m_logicalDevice  = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkCommandPool    m_commandPool    = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;

    std::unordered_map<SamplerType, VkSampler> m_samplers; // sampler type -> sampler

  public:
    VulkanResourceManager() = default;
    virtual ~VulkanResourceManager()
    {
        YA_CORE_INFO("VulkanResourceManager cleanup");
        cleanup();
    }

    void
         initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice,
                    VkCommandPool commandPool, VkQueue graphicsQueue);
    void cleanup();



    // Sampler management
    VkSampler getOrCreateSampler(SamplerType samplerType, VkSamplerCreateInfo *ci);
};
