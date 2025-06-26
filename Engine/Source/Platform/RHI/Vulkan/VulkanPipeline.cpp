#include "VulkanPipeline.h"
#include "Core/Log.h"
#include "VulkanUtils.h"
#include <array>


void VulkanPipeline::initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice)
{
    m_logicalDevice  = logicalDevice;
    m_physicalDevice = physicalDevice;

    _shaderProcessor = ShaderScriptProcessorFactory()
                           .withProcessorType(ShaderScriptProcessorFactory::EProcessorType::GLSL)
                           .withShaderStoragePath("Engine/Shader/GLSL")
                           .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
                           .FactoryNew<GLSLScriptProcessor>();

    queryPhysicalDeviceLimits(); // maxTextureSlots
    createDefaultSampler();
}

void VulkanPipeline::recreate(VkRenderPass renderPass, VkExtent2D extent)
{
    // Clean up old pipeline
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_logicalDevice, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }

    // Recreate with new parameters
    createGraphicsPipeline("VulkanTest.glsl", renderPass, extent);
}

void VulkanPipeline::cleanup()
{
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_logicalDevice, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }

    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_logicalDevice, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_logicalDevice, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_logicalDevice, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_defaultTextureSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_logicalDevice, m_defaultTextureSampler, nullptr);
        m_defaultTextureSampler = VK_NULL_HANDLE;
    }

    // Clean up textures
    for (auto &texture : m_textures) {
        if (texture) {
            if (texture->imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(m_logicalDevice, texture->imageView, nullptr);
            }
            if (texture->image != VK_NULL_HANDLE) {
                vkDestroyImage(m_logicalDevice, texture->image, nullptr);
            }
            if (texture->memory != VK_NULL_HANDLE) {
                vkFreeMemory(m_logicalDevice, texture->memory, nullptr);
            }
        }
    }
    m_textures.clear();
}

void VulkanPipeline::bindDescriptorSets(VkCommandBuffer commandBuffer)
{
    if (m_descriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(commandBuffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout,
                                0,
                                1,
                                &m_descriptorSet,
                                0,
                                nullptr);
    }
}

void VulkanPipeline::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{
        .binding            = 0,
        .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount    = 1,
        .stageFlags         = VK_SHADER_STAGE_VERTEX_BIT,
        .pImmutableSamplers = nullptr,
    };

    VkDescriptorSetLayoutBinding samplerLayoutBinding{
        .binding            = 1,
        .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount    = 1,
        .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr,
    };

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings    = bindings.data(),
    };

    VkResult result = vkCreateDescriptorSetLayout(m_logicalDevice, &layoutInfo, nullptr, &m_descriptorSetLayout);
    NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create descriptor set layout!");
}

void VulkanPipeline::createPipelineLayout()
{
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &m_descriptorSetLayout,
        .pushConstantRangeCount = 0,
    };

    VkResult result = vkCreatePipelineLayout(m_logicalDevice, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
    NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create pipeline layout!");
}

void VulkanPipeline::createDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 1,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes    = poolSizes.data(),
    };

    VkResult result = vkCreateDescriptorPool(m_logicalDevice, &poolInfo, nullptr, &m_descriptorPool);
    NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create descriptor pool!");
}

void VulkanPipeline::createDescriptorSets()
{
    VkDescriptorSetAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = m_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &m_descriptorSetLayout,
    };

    VkResult result = vkAllocateDescriptorSets(m_logicalDevice, &allocInfo, &m_descriptorSet);
    NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate descriptor sets!");
}

void VulkanPipeline::createDefaultSampler()
{
    VkSamplerCreateInfo samplerInfo{
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter               = VK_FILTER_LINEAR,
        .minFilter               = VK_FILTER_LINEAR,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias              = 0.0f,
        .anisotropyEnable        = VK_TRUE,
        .maxAnisotropy           = 16.0f,
        .compareEnable           = VK_FALSE,
        .compareOp               = VK_COMPARE_OP_ALWAYS,
        .minLod                  = 0.0f,
        .maxLod                  = 0.0f,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkResult result = vkCreateSampler(m_logicalDevice, &samplerInfo, nullptr, &m_defaultTextureSampler);
    NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create texture sampler!");
}

void VulkanPipeline::queryPhysicalDeviceLimits()
{
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    m_maxTextureSlots = properties.limits.maxPerStageDescriptorSamplers;
}

VkShaderModule VulkanPipeline::createShaderModule(const std::vector<uint32_t> &spv_binary)
{
    VkShaderModuleCreateInfo createInfo{
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spv_binary.size() * sizeof(uint32_t),
        .pCode    = spv_binary.data(),
    };

    VkShaderModule shaderModule;
    VkResult       result = vkCreateShaderModule(m_logicalDevice, &createInfo, nullptr, &shaderModule);
    NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create shader module!");

    return shaderModule;
}

// Stub implementations for remaining methods
void VulkanPipeline::updateDescriptorSets()
{
    // TODO: Implement descriptor set updates
}

void VulkanPipeline::createTexture(const std::string &path)
{
    // TODO: Implement texture creation
}
