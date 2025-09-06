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

    _render->setDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, _handle, setLayout.label);
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
{
    VK_DESTROY(DescriptorSetLayout, _render->getLogicalDevice(), _handle);
}



VulkanDescriptorPool::VulkanDescriptorPool(VulkanRender *render, const ya::DescriptorPoolCreateInfo &ci)
{
    _render = render;
    std::vector<VkDescriptorPoolSize> vkPoolSizes;
    for (const auto &size : ci.poolSizes) {
        vkPoolSizes.push_back(VkDescriptorPoolSize{
            .type            = toVk(size.type),
            .descriptorCount = size.descriptorCount,
        });
    }

    VkDescriptorPoolCreateInfo dspCI{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = 0,
        .maxSets       = ci.maxSets,
        .poolSizeCount = static_cast<uint32_t>(vkPoolSizes.size()),
        .pPoolSizes    = vkPoolSizes.data(),
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

bool VulkanDescriptorPool::allocateDescriptorSetN(const std::shared_ptr<VulkanDescriptorSetLayout> &layout, uint32_t count, std::vector<VkDescriptorSet> &set)
{
    if (set.size() < count) {
        set.resize(count);
    }
    std::vector<VkDescriptorSetLayout> sameLayouts(set.size(), layout->_handle);

    // 基于同一种layout，allocate n 个 set
    VkDescriptorSetAllocateInfo dsAI{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorPool     = _handle,
        .descriptorSetCount = static_cast<uint32_t>(set.size()),
        .pSetLayouts        = sameLayouts.data(),
    };

    VK_CALL(vkAllocateDescriptorSets(_render->getLogicalDevice(),
                                     &dsAI,
                                     set.data()));
    return true;
}
