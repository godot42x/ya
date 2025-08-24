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

struct VulkanRender;
struct VulkanRenderPass;
struct VulkanPipelineLayout
{
    VulkanRender    *_render         = nullptr;
    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    // std::vector<VkDescriptorSetLayout> _descriptorSetLayouts = {};

    // ya::PipelineLayout _ci;

    VulkanPipelineLayout() = default;
    VulkanPipelineLayout(VulkanRender *render)
        : _render(render) {}

    void create(const std::vector<ya::PushConstant>       pushConstants,
                const std::vector<VkDescriptorSetLayout> &layouts);

    auto getHandle() { return _pipelineLayout; }
    void cleanup();

    // ya::PipelineLayout getCI() const { return _ci; }
};



struct VulkanPipeline
{
  public:
    FName name;

    VkPipeline       _pipeline       = VK_NULL_HANDLE;
    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;

  private:


    GraphicsPipelineCreateInfo _ci;
    VulkanRender              *_render         = nullptr;
    VulkanRenderPass          *_renderPass     = nullptr;
    VulkanPipelineLayout      *_pipelineLayout = nullptr;

  public:
    ~VulkanPipeline() = default;
    VulkanPipeline(VulkanRender *render, VulkanRenderPass *renderPass, VulkanPipelineLayout *pipelineLayout)
    {
        _render         = render;
        _renderPass     = renderPass;
        _pipelineLayout = pipelineLayout;
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

    VkPipeline getHandle() const { return _pipeline; }


  private:
    // Pipeline creation helpers
    VkShaderModule createShaderModule(const std::vector<uint32_t> &spv_binary);
    void           createPipelineInternal();

    void queryPhysicalDeviceLimits();
};
