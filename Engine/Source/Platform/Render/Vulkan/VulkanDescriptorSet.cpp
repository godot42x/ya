#include "VulkanDescriptorSet.h"
#include "VulkanRender.h"
#include "VulkanUtils.h"
#include "utility.cc/ranges.h"


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
    createInternal(ci);
}

VulkanDescriptorPool::~VulkanDescriptorPool()
{
    resetPool();
    VK_DESTROY(DescriptorPool, _render->getDevice(), _handle);
}

void VulkanDescriptorPool::createInternal(DescriptorPoolCreateInfo const &ci)
{
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
    VK_CALL(vkCreateDescriptorPool(_render->getDevice(),
                                   &dspCI,
                                   _render->getAllocator(),
                                   &_handle));
}



void VulkanDescriptorPool::resetPool()
{
    if (_handle) {
        vkResetDescriptorPool(_render->getDevice(), _handle, 0);
    }
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

bool VulkanDescriptorPool::allocateDescriptorSetN(const std::shared_ptr<VulkanDescriptorSetLayout> &layout,
                                                  uint32_t                                          count,
                                                  std::vector<VkDescriptorSet>                     &set)
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

void VulkanDescriptorPool::cleanup()
{
}

void VulkanDescriptorHelper::updateDescriptorSets(
    const std::vector<ya::WriteDescriptorSet> &writes,
    const std::vector<ya::CopyDescriptorSet>  &copies)
{
    // Convert generic write descriptors to Vulkan-specific ones
    std::vector<VkWriteDescriptorSet>   vkWrites;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo>  imageInfos;

    // 预分配空间，避免扩容导致指针失效
    vkWrites.reserve(writes.size());

    // 预计算总数
    size_t totalBufferInfos = 0;
    size_t totalImageInfos  = 0;
    for (const auto &write : writes) {
        switch (write.descriptorType) {
        case EPipelineDescriptorType::UniformBuffer:
        case EPipelineDescriptorType::StorageBuffer:
            totalBufferInfos += write.bufferInfos.size();
            break;

        case EPipelineDescriptorType::Sampler:
        case EPipelineDescriptorType::CombinedImageSampler:
        case EPipelineDescriptorType::SampledImage:
        case EPipelineDescriptorType::StorageImage:
            totalImageInfos += write.imageInfos.size();
            break;

        case EPipelineDescriptorType::ENUM_MAX:
            break;
        }
    }
    bufferInfos.reserve(totalBufferInfos);
    imageInfos.reserve(totalImageInfos);

    for (const auto &write : writes) {


        VkWriteDescriptorSet vkWrite{
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = (VkDescriptorSet)(uintptr_t)write.dstSet,
            .dstBinding       = write.dstBinding,
            .dstArrayElement  = write.dstArrayElement,
            .descriptorCount  = write.descriptorCount,
            .descriptorType   = toVk(write.descriptorType),
            .pImageInfo       = nullptr,
            .pBufferInfo      = nullptr,
            .pTexelBufferView = nullptr,
        };

        switch (write.descriptorType) {
        case EPipelineDescriptorType::UniformBuffer:
        case EPipelineDescriptorType::StorageBuffer:
        {
            int startIdx = static_cast<int>(bufferInfos.size());
            for (const auto &bufInfo : write.bufferInfos) {
                bufferInfos.push_back(VkDescriptorBufferInfo{
                    .buffer = (VkBuffer)(uintptr_t)bufInfo.buffer.as<void *>(),
                    .offset = bufInfo.offset,
                    .range  = bufInfo.range,
                });
            }
            vkWrite.pBufferInfo = &bufferInfos[startIdx];
            vkWrites.push_back(std::move(vkWrite));

        } break;
        case EPipelineDescriptorType::Sampler:
        case EPipelineDescriptorType::CombinedImageSampler:
        case EPipelineDescriptorType::SampledImage:
        case EPipelineDescriptorType::StorageImage:
        {
            int startIdx = static_cast<int>(imageInfos.size());
            for (const auto &imgInfo : write.imageInfos) {
                imageInfos.push_back(VkDescriptorImageInfo{
                    .sampler     = (VkSampler)(uintptr_t)imgInfo.sampler.as<void *>(),
                    .imageView   = (VkImageView)(uintptr_t)imgInfo.imageView.as<void *>(),
                    .imageLayout = toVk(imgInfo.imageLayout),
                });
            }
            vkWrite.pImageInfo = &imageInfos[startIdx];
            vkWrites.push_back(std::move(vkWrite));

        } break;
        case EPipelineDescriptorType::ENUM_MAX:
            break;
        }
    }


    // validation
    for (const auto &[idx, write] : vkWrites | ut::enumerate) {
        YA_CORE_ASSERT(
            (write.pBufferInfo != nullptr) || (write.pImageInfo != nullptr) || (write.pTexelBufferView != nullptr),
            "VulkanDescriptorHelper::updateDescriptorSets, {}: write descriptor set has no valid info ptr",
            idx);
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