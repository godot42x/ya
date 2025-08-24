#include "VulkanDescriptorSet.h"
#include "VulkanRender.h"
#include "VulkanUtils.h"



VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VulkanRender *render, ya::DescriptorSetLayout setLayout)
{
    _render = render;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {};
    for (const auto &binding : setLayout.bindings) {
        bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding            = binding.binding,
            .descriptorType     = toVk(binding.descriptorType),
            .descriptorCount    = binding.descriptorCount,
            .stageFlags         = toVk(binding.stageFlags),
            .pImmutableSamplers = nullptr, // TODO: handle immutable samplers
        });
    }

    VkDescriptorSetLayoutCreateInfo ci{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = nullptr,
        .flags        = 0,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings    = bindings.data(),
    };

    VK_CALL(vkCreateDescriptorSetLayout(_render->getLogicalDevice(),
                                        &ci,
                                        _render->getAllocator(),
                                        &_handle));
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
{
    VK_DESTROY(DescriptorSetLayout, _render->getLogicalDevice(), _handle);
}

VulkanDescriptorPool::VulkanDescriptorPool(VulkanRender *render, uint32_t maxSets, const std::vector<VkDescriptorPoolSize> &poolSizes)
{
    _render = render;

    VkDescriptorPoolCreateInfo dspCI{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = 0,
        .maxSets       = maxSets,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes    = poolSizes.data(),
    };
    VK_CALL(vkCreateDescriptorPool(_render->getLogicalDevice(), &dspCI, _render->getAllocator(), &_handle));
}

VulkanDescriptorPool::~VulkanDescriptorPool()
{
    VK_DESTROY(DescriptorPool, _render->getLogicalDevice(), _handle);
}


void VulkanDescriptorPool::setDebugName(const char *name)
{
    _render->setDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, _handle, name);
}


bool VulkanDescriptorPool::allocateDescriptorSets(const std::vector<std::shared_ptr<VulkanDescriptorSetLayout>> &layouts,
                                                  std::vector<VkDescriptorSet>                                  &outSets)
{
    std::vector<VkDescriptorSetLayout> vkLayouts;
    for (const auto &layout : layouts) {
        vkLayouts.push_back(layout->_handle);
    }
    VkDescriptorSetAllocateInfo dsAI{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorPool     = _handle,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts        = vkLayouts.data(),
    };

    outSets.resize(layouts.size());

    VK_CALL(vkAllocateDescriptorSets(_render->getLogicalDevice(),
                                     &dsAI,
                                     outSets.data()));
    return true;
}
