
#pragma once
#include "Core/Base.h"
#include "Render/RenderDefines.h"

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


    VulkanDescriptorPool(VulkanRender *render, const ya::DescriptorPoolCreateInfo &ci);
    virtual ~VulkanDescriptorPool();

    // allocate n same set from 1 set layout
    bool allocateDescriptorSetN(const std::shared_ptr<VulkanDescriptorSetLayout> &layout, uint32_t count, std::vector<VkDescriptorSet> &set);

    void setDebugName(const char *name);
};


struct VulkanDescriptor
{

    static void updateSets(
        VkDevice                                 device,
        const std::vector<VkWriteDescriptorSet> &descriptorWrites,
        const std::vector<VkCopyDescriptorSet>  &descriptorCopies)
    {
        vkUpdateDescriptorSets(
            device,
            static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(),
            static_cast<uint32_t>(descriptorCopies.size()),
            descriptorCopies.data());
    }



    static VkWriteDescriptorSet genBufferWrite(
        VkDescriptorSet               dstSet,
        uint32_t                      dstBinding,
        uint32_t                      dstArrayElement,
        VkDescriptorType              descriptorType,
        const VkDescriptorBufferInfo *pBufferInfo,
        uint32_t                      descriptorCount = 1)
    {
        return genWriteDescriptorSet(
            dstSet,
            dstBinding,
            dstArrayElement,
            descriptorType,
            descriptorCount,
            pBufferInfo,
            nullptr,
            nullptr);
    }

    static VkWriteDescriptorSet genImageWrite(
        VkDescriptorSet              dstSet,
        uint32_t                     dstBinding,
        uint32_t                     dstArrayElement,
        VkDescriptorType             descriptorType,
        const VkDescriptorImageInfo *pImageInfo,
        uint32_t                     descriptorCount = 1)
    {
        return genWriteDescriptorSet(
            dstSet,
            dstBinding,
            dstArrayElement,
            descriptorType,
            descriptorCount,
            nullptr,
            pImageInfo,
            nullptr);
    }

    static VkWriteDescriptorSet genWriteTexelBuffer(
        VkDescriptorSet     dstSet,
        uint32_t            dstBinding,
        uint32_t            dstArrayElement,
        VkDescriptorType    descriptorType,
        const VkBufferView *pTexelBufferView,
        uint32_t            descriptorCount = 1)
    {
        return genWriteDescriptorSet(
            dstSet,
            dstBinding,
            dstArrayElement,
            descriptorType,
            descriptorCount,
            nullptr,
            nullptr,
            pTexelBufferView);
    }


    static VkWriteDescriptorSet genWriteDescriptorSet(
        VkDescriptorSet               dstSet,
        uint32_t                      dstBinding,
        uint32_t                      dstArrayElement,
        VkDescriptorType              descriptorType,
        uint32_t                      descriptorCount,
        const VkDescriptorBufferInfo *pBufferInfo,
        const VkDescriptorImageInfo  *pImageInfo,
        const VkBufferView           *pTexelBufferView)
    {
        VkWriteDescriptorSet write{
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = dstSet,
            .dstBinding       = dstBinding,
            .dstArrayElement  = dstArrayElement,
            .descriptorCount  = descriptorCount,
            .descriptorType   = descriptorType,
            .pImageInfo       = pImageInfo,
            .pBufferInfo      = pBufferInfo,
            .pTexelBufferView = pTexelBufferView,
        };
        return write;
    }
};