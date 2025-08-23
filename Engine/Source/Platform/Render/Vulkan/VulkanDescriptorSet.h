
#pragma once
#include "Core/Base.h"
#include "Render/Render.h"

#include "vulkan/vulkan.h"


struct VulkanRender;

struct VulkanDescriptorSetLayout
{
    VulkanRender           *_render = nullptr;
    VkDescriptorSetLayout   _handle = VK_NULL_HANDLE;
    ya::DescriptorSetLayout _setLayoutInfo;

    VulkanDescriptorSetLayout(VulkanRender *render, ya::DescriptorSetLayout setLayout);
    virtual ~VulkanDescriptorSetLayout();

    VkDescriptorSetLayout getHandle() const { return _handle; }
};

struct VulkanDescriptorPool
{
    VulkanRender    *_render = nullptr;
    VkDescriptorPool _handle = VK_NULL_HANDLE;


    VulkanDescriptorPool(VulkanRender *render, uint32_t maxSets, const std::vector<VkDescriptorPoolSize> &poolSizes);
    virtual ~VulkanDescriptorPool();

    bool allocateDescriptorSets(const std::vector<std::shared_ptr<VulkanDescriptorSetLayout>> &layouts, std::vector<VkDescriptorSet> &sets);

    void setDebugName(const char *name);
};