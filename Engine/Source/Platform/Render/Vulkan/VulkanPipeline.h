#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "Core/Base.h"
#include "Core/FName.h"
#include "Render/Render.h"
#include "Render/Shader.h"


// Texture management (shared across all pipelines)
struct VulkanTexture2D
{
    VkImage        image     = VK_NULL_HANDLE;
    VkImageView    imageView = VK_NULL_HANDLE;
    VkDeviceMemory memory    = VK_NULL_HANDLE;
};


struct VulkanRender;
struct VulkanRenderPass;
struct VulkanPipelineLayout
{
    VulkanRender         *_render              = nullptr;
    VkPipelineLayout      _pipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;

    GraphicsPipelineLayoutCreateInfo _ci;

    VulkanPipelineLayout() = default;
    VulkanPipelineLayout(VulkanRender *render)
        : _render(render) {}

    void create(GraphicsPipelineLayoutCreateInfo);
    auto getHandle() { return _pipelineLayout; }
    void cleanup();
};



struct VulkanPipeline
{
  public:
    FName name;

    VkPipeline _pipeline = VK_NULL_HANDLE;
    // VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    // VkDescriptorSet  _descriptorSet  = VK_NULL_HANDLE;


    // Resource tracking for cleanup
    // std::vector<std::shared_ptr<VulkanTexture2D>> textures;

  private:
    std::shared_ptr<GLSLScriptProcessor> _shaderProcessor;


    GraphicsPipelineCreateInfo _ci;
    VulkanRender              *_render         = nullptr;
    VulkanRenderPass          *_renderPass     = nullptr;
    VulkanPipelineLayout      *_pipelineLayout = nullptr;

  public:
    ~VulkanPipeline() = default;
    VulkanPipeline(VulkanRender *render, VulkanRenderPass *renderPass, VulkanPipelineLayout *pipelineLayout)
    {
        _render          = render;
        _renderPass      = renderPass;
        _pipelineLayout  = pipelineLayout;
        _shaderProcessor = ShaderScriptProcessorFactory()
                               .withProcessorType(ShaderScriptProcessorFactory::EProcessorType::GLSL)
                               .withShaderStoragePath("Engine/Shader/GLSL")
                               .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
                               .FactoryNew<GLSLScriptProcessor>();
        queryPhysicalDeviceLimits(); // maxTextureSlots
    }


    void cleanup();
    bool recreate(const GraphicsPipelineCreateInfo &ci);

    void bind(VkCommandBuffer commandBuffer)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
    }
    void bind(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint)
    {
        vkCmdBindPipeline(commandBuffer, bindPoint, _pipeline);
    }

    void bindLayoutSets(VkCommandBuffer commandBuffer, uint32_t firstSet = 0, uint32_t setCount = 1, const VkDescriptorSet *pDescriptorSets = nullptr)
    {
        UNIMPLEMENTED();
        vkCmdBindDescriptorSets(commandBuffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                _pipelineLayout->getHandle(),
                                firstSet,
                                setCount,
                                pDescriptorSets,
                                0,
                                nullptr);
    }


  private:
    // Pipeline creation helpers
    VkShaderModule createShaderModule(const std::vector<uint32_t> &spv_binary);
    void           createDescriptorSetLayout();
    void           createPipelineLayout();
    void           createDescriptorPool();
    void           createDescriptorSets();
    void           createPipelineInternal();

    void queryPhysicalDeviceLimits();
};
