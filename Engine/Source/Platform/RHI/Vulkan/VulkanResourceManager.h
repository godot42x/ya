#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>


class VulkanResourceManager
{
  private:
    VkDevice         m_logicalDevice  = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkCommandPool    m_commandPool    = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;

    // Descriptor resources
    VkDescriptorPool             m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    // Texture resources
    struct VulkanTexture2D
    {
        VkImage        image     = VK_NULL_HANDLE;
        VkImageView    imageView = VK_NULL_HANDLE;
        VkDeviceMemory memory    = VK_NULL_HANDLE;
        VkSampler      sampler   = VK_NULL_HANDLE;
    };
    std::vector<std::shared_ptr<VulkanTexture2D>> m_textures;

    // Buffer resources
    struct VulkanBuffer
    {
        VkBuffer       buffer     = VK_NULL_HANDLE;
        VkDeviceMemory memory     = VK_NULL_HANDLE;
        VkDeviceSize   size       = 0;
        void          *mappedData = nullptr;
    };
    std::vector<std::shared_ptr<VulkanBuffer>> m_buffers;

    // Samplers
    std::unordered_map<uint32_t, VkSampler> m_samplers; // sampler type -> sampler

  public:
    VulkanResourceManager()  = default;
    ~VulkanResourceManager() = default;

    void initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice,
                    VkCommandPool commandPool, VkQueue graphicsQueue);
    void cleanup();

    // Descriptor management
    void            createDescriptorPool(uint32_t maxSets = 1000);
    VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout layout);
    void            updateDescriptorSet(VkDescriptorSet                          descriptorSet,
                                        const std::vector<VkWriteDescriptorSet> &writes);

    // Texture management
    std::shared_ptr<VulkanTexture2D> createTexture2D(const std::string &path);
    std::shared_ptr<VulkanTexture2D> createTexture2D(uint32_t width, uint32_t height,
                                                     VkFormat format, const void *data = nullptr);

    // Buffer management
    std::shared_ptr<VulkanBuffer> createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                               VkMemoryPropertyFlags properties);
    void                          updateBuffer(std::shared_ptr<VulkanBuffer> buffer, const void *data, VkDeviceSize size);

    // Sampler management
    VkSampler getOrCreateSampler(uint32_t samplerType);
    VkSampler createSampler(VkFilter magFilter, VkFilter minFilter,
                            VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

    // Getters
    VkDescriptorPool getDescriptorPool() const { return m_descriptorPool; }

  private:
    void createDefaultSamplers();
};
