#include "VulkanDescriptorSet.h"
#include "VulkanRender.h"
#include "VulkanUtils.h"


namespace ya
{


VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VulkanRender *render, ya::DescriptorSetLayout setLayout)
{
    _render        = render;
    _setLayoutInfo = setLayout;

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

    VK_CALL(vkCreateDescriptorSetLayout(_render->getDevice(),
                                        &ci,
                                        _render->getAllocator(),
                                        &_handle));

    _render->setDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, _handle, setLayout.label);
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
{
    VK_DESTROY(DescriptorSetLayout, _render->getDevice(), _handle);
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
    VK_CALL(vkCreateDescriptorPool(_render->getDevice(), &dspCI, _render->getAllocator(), &_handle));
}

VulkanDescriptorPool::~VulkanDescriptorPool()
{
    VK_DESTROY(DescriptorPool, _render->getDevice(), _handle);
}



void VulkanDescriptorPool::reset()
{
    vkResetDescriptorPool(_render->getDevice(), _handle, 0);
}

void VulkanDescriptorPool::setDebugName(const char *name)
{
    _render->setDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, _handle, name);
}

bool VulkanDescriptorPool::allocateDescriptorSets(
    const std::shared_ptr<ya::IDescriptorSetLayout> &layout,
    uint32_t                                         count,
    std::vector<ya::DescriptorSetHandle>            &outSets)
{
    // Cast to Vulkan-specific type
    auto vkLayout = std::static_pointer_cast<VulkanDescriptorSetLayout>(layout);

    if (outSets.size() < count) {
        outSets.resize(count);
    }

    std::vector<VkDescriptorSet>       vkSets(count);
    std::vector<VkDescriptorSetLayout> sameLayouts(count, vkLayout->_handle);

    VkDescriptorSetAllocateInfo dsAI{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorPool     = _handle,
        .descriptorSetCount = count,
        .pSetLayouts        = sameLayouts.data(),
    };

    VK_CALL(vkAllocateDescriptorSets(_render->getDevice(), &dsAI, vkSets.data()));

    // Convert VkDescriptorSet handles to DescriptorSetHandle
    for (size_t i = 0; i < count; ++i) {
        outSets[i] = ya::DescriptorSetHandle((void *)(uintptr_t)vkSets[i]);
    }

    return true;
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

    VK_CALL(vkAllocateDescriptorSets(_render->getDevice(),
                                     &dsAI,
                                     set.data()));
    return true;
}

void VulkanDescriptor::updateDescriptorSets(
    const std::vector<ya::WriteDescriptorSet> &writes,
    const std::vector<ya::CopyDescriptorSet>  &copies)
{
    // Convert generic write descriptors to Vulkan-specific ones
    std::vector<VkWriteDescriptorSet>   vkWrites;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo>  imageInfos;

    vkWrites.reserve(writes.size());

    for (const auto &write : writes) {
        VkWriteDescriptorSet vkWrite{
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext           = nullptr,
            .dstSet          = (VkDescriptorSet)(uintptr_t)write.dstSet,
            .dstBinding      = write.dstBinding,
            .dstArrayElement = write.dstArrayElement,
            .descriptorCount = write.descriptorCount,
            .descriptorType  = toVk(write.descriptorType),
        };

        if (write.pBufferInfo) {
            size_t startIdx = bufferInfos.size();
            for (uint32_t i = 0; i < write.descriptorCount; ++i) {
                VkDescriptorBufferInfo bufInfo{};
                bufInfo.buffer = (VkBuffer)(uintptr_t)write.pBufferInfo[i].buffer;
                bufInfo.offset = write.pBufferInfo[i].offset;
                bufInfo.range  = write.pBufferInfo[i].range;
                bufferInfos.push_back(bufInfo);
            }
            vkWrite.pBufferInfo = &bufferInfos[startIdx];
        }
        else if (write.pImageInfo) {
            size_t startIdx = imageInfos.size();
            for (uint32_t i = 0; i < write.descriptorCount; ++i) {
                VkDescriptorImageInfo imgInfo{};
                imgInfo.sampler     = write.pImageInfo[i].sampler.as<VkSampler>();
                imgInfo.imageView   = write.pImageInfo[i].imageView.as<VkImageView>();
                imgInfo.imageLayout = toVk(write.pImageInfo[i].imageLayout);
                imageInfos.push_back(imgInfo);
            }
            vkWrite.pImageInfo = &imageInfos[startIdx];
        }
        else if (write.pTexelBufferView) {
            vkWrite.pTexelBufferView = (const VkBufferView *)write.pTexelBufferView;
        }

        vkWrites.push_back(vkWrite);
    }

    // Convert copy descriptors (if any)
    std::vector<VkCopyDescriptorSet> vkCopies;
    vkCopies.reserve(copies.size());

    for (const auto &copy : copies) {
        VkCopyDescriptorSet vkCopy{};
        vkCopy.sType           = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
        vkCopy.pNext           = nullptr;
        vkCopy.srcSet          = (VkDescriptorSet)(uintptr_t)copy.srcSet;
        vkCopy.srcBinding      = copy.srcBinding;
        vkCopy.srcArrayElement = copy.srcArrayElement;
        vkCopy.dstSet          = (VkDescriptorSet)(uintptr_t)copy.dstSet;
        vkCopy.dstBinding      = copy.dstBinding;
        vkCopy.dstArrayElement = copy.dstArrayElement;
        vkCopy.descriptorCount = copy.descriptorCount;
        vkCopies.push_back(vkCopy);
    }

    vkUpdateDescriptorSets(
        _render->getDevice(),
        static_cast<uint32_t>(vkWrites.size()),
        vkWrites.data(),
        static_cast<uint32_t>(vkCopies.size()),
        vkCopies.data());
}

} // namespace ya