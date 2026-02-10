#include "DescriptorSet.h"
#include "Platform/Render/Vulkan/VulkanDescriptorSet.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Render.h"


namespace ya
{

// Factory method to create descriptor set layout based on render backend
std::shared_ptr<IDescriptorSetLayout> IDescriptorSetLayout::create(IRender *render, const DescriptorSetLayoutDesc &layout)
{
    switch (render->getAPI()) {
    case ERenderAPI::Vulkan:
        return makeShared<VulkanDescriptorSetLayout>(static_cast<VulkanRender *>(render), layout);
    case ERenderAPI::None:
    default:
        YA_CORE_ERROR("Unsupported render API for descriptor set layout creation");
        return nullptr;
    }
}

std::vector<stdptr<IDescriptorSetLayout>> IDescriptorSetLayout::create(IRender *render, std::vector<DescriptorSetLayoutDesc> descriptorSetLayouts)
{
    switch (render->getAPI()) {
    case ERenderAPI::Vulkan:
    {
        std::vector<stdptr<IDescriptorSetLayout>> vkLayouts;
        for (const auto &layout : descriptorSetLayouts) {
            vkLayouts.push_back(makeShared<VulkanDescriptorSetLayout>(render->as<VulkanRender>(), layout));
        }
        return vkLayouts;

    } break;
    case ERenderAPI::None:
    default:
        YA_CORE_ERROR("Unsupported render API for descriptor set layout creation");
        return {};
    }
    return {};
}

// Factory method to create descriptor pool based on render backend
std::shared_ptr<IDescriptorPool> IDescriptorPool::create(IRender *render, const DescriptorPoolCreateInfo &ci)
{
    switch (render->getAPI()) {
    case ERenderAPI::Vulkan:
        return makeShared<VulkanDescriptorPool>(static_cast<VulkanRender *>(render), ci);
    case ERenderAPI::None:
    default:
        YA_CORE_ERROR("Unsupported render API for descriptor pool creation");
        return nullptr;
    }
}

// Implementation file for descriptor set interfaces
// Concrete implementations are in platform-specific files (e.g., VulkanDescriptorSet.cpp)

} // namespace ya
