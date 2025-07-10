#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "Core/Base.h"
#include "Core/FName.h"
#include "RHI/Render.h"
#include "Render/Shader.h"


// Texture management (shared across all pipelines)
struct VulkanTexture2D
{
    VkImage        image     = VK_NULL_HANDLE;
    VkImageView    imageView = VK_NULL_HANDLE;
    VkDeviceMemory memory    = VK_NULL_HANDLE;
};



class VulkanPipelineManager
{
  public:
    struct PipelineInfo
    {
        FName                      name;
        VkPipeline                 pipeline            = VK_NULL_HANDLE;
        VkPipelineLayout           pipelineLayout      = VK_NULL_HANDLE;
        VkDescriptorSetLayout      descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool           descriptorPool      = VK_NULL_HANDLE;
        VkDescriptorSet            descriptorSet       = VK_NULL_HANDLE;
        GraphicsPipelineCreateInfo config;

        // Resource tracking for cleanup
        std::vector<std::shared_ptr<VulkanTexture2D>> textures;
    };

  private:
    std::shared_ptr<GLSLScriptProcessor> _shaderProcessor;

    VkDevice         m_logicalDevice  = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkExtent2D       m_extent;

    // Pipeline management - using FName for O(1) lookups
    std::unordered_map<FName, std::unique_ptr<PipelineInfo>> m_pipelines;
    FName                                                    m_activePipelineName;

    // Shared resources
    VkSampler m_defaultTextureSampler = VK_NULL_HANDLE;
    int       m_maxTextureSlots       = -1;

  public:
    VulkanPipelineManager()  = default;
    ~VulkanPipelineManager() = default;

    void initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice);
    void cleanup();

    // Pipeline creation and management - now using FName for optimized lookups
    bool createPipelineImpl(const FName &name, const GraphicsPipelineCreateInfo &config, VkRenderPass renderPass, VkExtent2D extent);

    bool deletePipeline(const FName &name);
    void recreatePipeline(const FName &name, VkRenderPass renderPass, VkExtent2D extent);
    void recreateAllPipelines(VkRenderPass renderPass, VkExtent2D extent);

    // Pipeline binding and usage
    bool bindPipeline(VkCommandBuffer commandBuffer, const FName &name);
    void bindDescriptorSets(VkCommandBuffer commandBuffer, const FName &name);
    void setActivePipeline(const FName &name);

    // Getters
    PipelineInfo         *getPipelineInfo(const FName &name);
    VkPipeline            getPipeline(const FName &name);
    VkPipelineLayout      getPipelineLayout(const FName &name);
    VkDescriptorSetLayout getDescriptorSetLayout(const FName &name);

    // Get list of all pipeline names
    std::vector<FName> getPipelineNames() const;

    // Check if pipeline exists
    bool hasPipeline(const FName &name) const;

    // Convenience overloads for string parameters
    bool createPipeline(const std::string &name, const GraphicsPipelineCreateInfo &config,
                        VkRenderPass renderPass, VkExtent2D extent)
    {
        return createPipelineImpl(FName(name), config, renderPass, extent);
    }
    bool deletePipeline(const std::string &name) { return deletePipeline(FName(name)); }
    void recreatePipeline(const std::string &name, VkRenderPass renderPass, VkExtent2D extent)
    {
        recreatePipeline(FName(name), renderPass, extent);
    }
    bool bindPipeline(VkCommandBuffer commandBuffer, const std::string &name)
    {
        return bindPipeline(commandBuffer, FName(name));
    }
    void bindDescriptorSets(VkCommandBuffer commandBuffer, const std::string &name)
    {
        bindDescriptorSets(commandBuffer, FName(name));
    }
    void                  setActivePipeline(const std::string &name) { setActivePipeline(FName(name)); }
    PipelineInfo         *getPipelineInfo(const std::string &name) { return getPipelineInfo(FName(name)); }
    VkPipeline            getPipeline(const std::string &name) { return getPipeline(FName(name)); }
    VkPipelineLayout      getPipelineLayout(const std::string &name) { return getPipelineLayout(FName(name)); }
    VkDescriptorSetLayout getDescriptorSetLayout(const std::string &name) { return getDescriptorSetLayout(FName(name)); }
    bool                  hasPipeline(const std::string &name) const { return hasPipeline(FName(name)); }

    // Texture management (shared across pipelines)
    void createTexture(const std::string &path);
    void createDefaultSampler();


  private:
    // Pipeline creation helpers
    VkShaderModule createShaderModule(const std::vector<uint32_t> &spv_binary);
    void           createDescriptorSetLayout(PipelineInfo &pipelineInfo, const GraphicsPipelineCreateInfo &config);
    void           createPipelineLayout(PipelineInfo &pipelineInfo);
    void           createDescriptorPool(PipelineInfo &pipelineInfo);
    void           createDescriptorSets(PipelineInfo &pipelineInfo);
    void           createGraphicsPipelineInternal(PipelineInfo &pipelineInfo, const GraphicsPipelineCreateInfo &config,
                                                  VkRenderPass renderPass, VkExtent2D extent);

    void queryPhysicalDeviceLimits();
};
