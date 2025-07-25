#include <array>

#include "Core/Log.h"
#include "VulkanPipeline.h"
#include "VulkanRender.h"
#include "VulkanUtils.h"



void VulkanPipelineLayout::create(GraphicsPipelineLayoutCreateInfo ci)
{
    _ci = ci;

    std::vector<VkPushConstantRange> ranges;
    for (const auto &pushConstant : _ci.pushConstants) {
        ranges.push_back({
            .stageFlags = toVk(pushConstant.stageFlags),
            .offset     = pushConstant.offset,
            .size       = pushConstant.size,
        });
    }

    // currently only one layout
    std::vector<VkDescriptorSetLayoutBinding> bindings = {};
    for (const auto &setLayout : _ci.descriptorSetLayouts) {
        for (const auto &binding : setLayout.bindings) {
            bindings.push_back({
                .binding            = binding.binding,
                .descriptorType     = toVk(binding.descriptorType),
                .descriptorCount    = binding.descriptorCount,
                .stageFlags         = toVk(binding.stageFlags),
                .pImmutableSamplers = nullptr, // TODO: handle immutable samplers
            });
        }
    }

    VkDescriptorSetLayoutCreateInfo setLayoutCI{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = nullptr,
        .flags        = 0,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings    = bindings.data(),
    };

    vkCreateDescriptorSetLayout(_render->getLogicalDevice(), &setLayoutCI, nullptr, &_descriptorSetLayout);

    VkPipelineLayoutCreateInfo layoutCI{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .setLayoutCount         = 1,
        .pSetLayouts            = &_descriptorSetLayout,
        .pushConstantRangeCount = static_cast<uint32_t>(ranges.size()),
        .pPushConstantRanges    = ranges.data(),
    };

    VkResult result = vkCreatePipelineLayout(_render->getLogicalDevice(), &layoutCI, nullptr, &_pipelineLayout);
    if (result != VK_SUCCESS) {
        NE_CORE_ERROR("Failed to create Vulkan pipeline layout: {}", result);
        return; // false
    }

    NE_CORE_INFO("Vulkan pipeline layout created successfully: {}", (uintptr_t)_pipelineLayout);
}

void VulkanPipelineLayout::cleanup()
{
    VK_DESTROY(PipelineLayout, _render->getLogicalDevice(), _pipelineLayout);
    VK_DESTROY(DescriptorSetLayout, _render->getLogicalDevice(), _descriptorSetLayout);
}



void VulkanPipeline::cleanup()
{
    VK_DESTROY(Pipeline, _render->getLogicalDevice(), _pipeline);
}

bool VulkanPipeline::recreate(const GraphicsPipelineCreateInfo &ci)
{
    _ci = ci;

    // createDescriptorSetLayout();
    // createPipelineLayout();
    // createDescriptorPool();
    // createDescriptorSets();
    createPipelineInternal();

    return true;
}


// Pipeline creation helper methods
void VulkanPipeline::createDescriptorSetLayout()
{
    // // For now, use default descriptor layout - can be extended based on config
    // VkDescriptorSetLayoutBinding uboLayoutBinding{
    //     .binding            = 0,
    //     .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    //     .descriptorCount    = 1,
    //     .stageFlags         = VK_SHADER_STAGE_VERTEX_BIT,
    //     .pImmutableSamplers = nullptr,
    // };

    // VkDescriptorSetLayoutBinding samplerLayoutBinding{
    //     .binding            = 1,
    //     .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    //     .descriptorCount    = 1,
    //     .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
    //     .pImmutableSamplers = nullptr,
    // };

    // std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};

    // VkDescriptorSetLayoutCreateInfo layoutInfo{
    //     .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    //     .bindingCount = static_cast<uint32_t>(bindings.size()),
    //     .pBindings    = bindings.data(),
    // };

    // VkResult ret = vkCreateDescriptorSetLayout(_render->getLogicalDevice(), &layoutInfo, nullptr, &_descriptorSetLayout);
    // if (ret != VK_SUCCESS) {
    //     NE_CORE_ERROR("Failed to create descriptor set layout: {}", ret);
    //     return;
    // }
}

void VulkanPipeline::createPipelineLayout()
{
    // VkPipelineLayoutCreateInfo pipelineLayoutInfo{
    //     .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    //     .setLayoutCount         = 1,
    //     .pSetLayouts            = &_descriptorSetLayout,
    //     .pushConstantRangeCount = 0,
    // };

    // VkResult result = vkCreatePipelineLayout(_render->getLogicalDevice(), &pipelineLayoutInfo, nullptr, &_pipelineLayout);
    // NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create pipeline layout!");
}

void VulkanPipeline::createDescriptorPool()
{
    // std::array<VkDescriptorPoolSize, 2> poolSizes{};
    // poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    // poolSizes[0].descriptorCount = 1;
    // poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // poolSizes[1].descriptorCount = 1;

    // VkDescriptorPoolCreateInfo poolInfo{
    //     .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    //     .maxSets       = 1,
    //     .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
    //     .pPoolSizes    = poolSizes.data(),
    // };

    // VkResult result = vkCreateDescriptorPool(_render->getLogicalDevice(), &poolInfo, nullptr, &_descriptorPool);
    // NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create descriptor pool!");
}

void VulkanPipeline::createDescriptorSets()
{
    // VkDescriptorSetAllocateInfo allocInfo{
    //     .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    //     .descriptorPool     = _descriptorPool,
    //     .descriptorSetCount = 1,
    //     .pSetLayouts        = &_descriptorSetLayout,
    // };

    // VkResult result = vkAllocateDescriptorSets(_render->getLogicalDevice(), &allocInfo, &_descriptorSet);
    // NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate descriptor sets!");
}

void VulkanPipeline::createPipelineInternal()
{
    // Process shader
    auto stage2Spirv_opt = _shaderProcessor->process(_ci.shaderCreateInfo.shaderName);
    if (!stage2Spirv_opt.has_value()) {
        NE_CORE_ERROR("Failed to process shader: {}", _ci.shaderCreateInfo.shaderName);
        return;
    }
    auto &stage2Spirv = stage2Spirv_opt.value();

    // Create shader modules
    auto vertShaderModule = createShaderModule(stage2Spirv[EShaderStage::Vertex]);
    auto fragShaderModule = createShaderModule(stage2Spirv[EShaderStage::Fragment]);

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
        VkPipelineShaderStageCreateInfo{
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertShaderModule,
            .pName  = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragShaderModule,
            .pName  = "main",
        },
    };

    // Configure vertex input based on configuration

    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
    std::vector<VkVertexInputBindingDescription>   vertexBindingDescriptions;

    const auto &config = _ci.shaderCreateInfo;

    if (_ci.shaderCreateInfo.bDeriveFromShader) {
        // Get vertex input info from shader reflection
        auto vertexReflectInfo = _shaderProcessor->reflect(EShaderStage::Vertex, stage2Spirv[EShaderStage::Vertex]);

        auto spirvType2VulkanFormat = [](const auto &type) -> VkFormat {
            if (type.vecsize == 3 && type.basetype == 0)
                return VK_FORMAT_R32G32B32_SFLOAT;
            if (type.vecsize == 4 && type.basetype == 0)
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            if (type.vecsize == 2 && type.basetype == 0)
                return VK_FORMAT_R32G32_SFLOAT;
            return VK_FORMAT_R32G32B32_SFLOAT; // Default fallback
        };

        for (auto &input : vertexReflectInfo.inputs) {
            vertexAttributeDescriptions.push_back({
                .location = input.location,
                .binding  = 0,
                .format   = spirvType2VulkanFormat(input.format),
                .offset   = input.offset,
            });
        }

        if (!vertexAttributeDescriptions.empty()) {
            vertexBindingDescriptions.push_back({
                .binding   = 0,
                .stride    = vertexReflectInfo.inputs.back().offset + vertexReflectInfo.inputs.back().size,
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            });
        }
    }
    else {
        // Use provided vertex layout configuration
        for (const auto &bufferDesc : config.vertexBufferDescs) {
            vertexBindingDescriptions.push_back({
                .binding   = bufferDesc.slot,
                .stride    = bufferDesc.pitch,
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            });
        }

        for (const auto &attr : config.vertexAttributes) {
            vertexAttributeDescriptions.push_back({
                .location = attr.location,
                .binding  = attr.bufferSlot,
                .format   = toVk(attr.format),
                .offset   = attr.offset,
            });
        }
    }

    VkPipelineVertexInputStateCreateInfo vertexInputStateCI{
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = static_cast<uint32_t>(vertexBindingDescriptions.size()),
        .pVertexBindingDescriptions      = vertexBindingDescriptions.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size()),
        .pVertexAttributeDescriptions    = vertexAttributeDescriptions.data(),
    };

    // Configure input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .topology               = toVk(_ci.primitiveType),
        .primitiveRestartEnable = VK_FALSE, // No primitive restart for now
    };


    VkViewport defaultViewport{
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = 100.f,
        .height   = 100.f,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D defaultScissor{
        .offset = {0, 0},
        .extent = {100, 100},
    };
    VkPipelineViewportStateCreateInfo viewportStateCI{
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports    = &defaultViewport,
        .scissorCount  = 1,
        .pScissors     = &defaultScissor,
    };

    VkPipelineRasterizationStateCreateInfo rasterizationStateCI{
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = _ci.rasterizationState.bDepthClampEnable,
        .rasterizerDiscardEnable = _ci.rasterizationState.bRasterizerDiscardEnable,
        .polygonMode             = toVk(_ci.rasterizationState.polygonMode),
        .cullMode                = toVk(_ci.rasterizationState.cullMode),
        .frontFace               = toVk(_ci.rasterizationState.frontFace),
        .depthBiasEnable         = _ci.rasterizationState.bDepthBiasEnable,
        .depthBiasConstantFactor = _ci.rasterizationState.depthBiasConstantFactor,
        .depthBiasClamp          = _ci.rasterizationState.depthBiasClamp,
        .lineWidth               = _ci.rasterizationState.lineWidth,
    };

    VkPipelineMultisampleStateCreateInfo multiSamplingStateCI{
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples  = toVk(_ci.multisampleState.sampleCount),
        .sampleShadingEnable   = _ci.multisampleState.bSampleShadingEnable,
        .minSampleShading      = _ci.multisampleState.minSampleShading,
        .alphaToCoverageEnable = _ci.multisampleState.bAlphaToCoverageEnable,
        .alphaToOneEnable      = _ci.multisampleState.bAlphaToOneEnable,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = _ci.depthStencilState.bDepthTestEnable,
        .depthWriteEnable      = _ci.depthStencilState.bDepthWriteEnable,
        .depthCompareOp        = toVk(_ci.depthStencilState.depthCompareOp),
        .depthBoundsTestEnable = _ci.depthStencilState.bDepthBoundsTestEnable,
        .stencilTestEnable     = _ci.depthStencilState.bStencilTestEnable,
        .minDepthBounds        = _ci.depthStencilState.minDepthBounds,
        .maxDepthBounds        = _ci.depthStencilState.maxDepthBounds,

    };

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    for (const auto &attachment : _ci.colorBlendState.attachments) {
        VkPipelineColorBlendAttachmentState colorBlendAttachment{
            .blendEnable         = attachment.bBlendEnable ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = toVk(attachment.srcColorBlendFactor),
            .dstColorBlendFactor = toVk(attachment.dstColorBlendFactor),
            .colorBlendOp        = toVk(attachment.colorBlendOp),
            .srcAlphaBlendFactor = toVk(attachment.srcAlphaBlendFactor),
            .dstAlphaBlendFactor = toVk(attachment.dstAlphaBlendFactor),
            .alphaBlendOp        = toVk(attachment.alphaBlendOp),
            .colorWriteMask      = toVk(attachment.colorWriteMask),
        };
        colorBlendAttachments.push_back(colorBlendAttachment);
    }

    VkPipelineColorBlendStateCreateInfo colorBlendingStateCI{
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0,
        .logicOpEnable   = _ci.colorBlendState.bLogicOpEnable ? VK_TRUE : VK_FALSE,
        .logicOp         = toVk(_ci.colorBlendState.logicOp),
        .attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size()),
        .pAttachments    = colorBlendAttachments.data(),
        .blendConstants  = {
            _ci.colorBlendState.blendConstants[0],
            _ci.colorBlendState.blendConstants[1],
            _ci.colorBlendState.blendConstants[2],
            _ci.colorBlendState.blendConstants[3],
        },
    };

    std::vector<VkDynamicState> dynamicStates = {};
    if (_ci.dynamicFeatures & EPipelineDynamicFeature::DepthTest) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE);
    }
    if (_ci.dynamicFeatures & EPipelineDynamicFeature::AlphaBlend) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
    }

    VkPipelineDynamicStateCreateInfo dynamicStateCI{
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext             = nullptr,
        .flags             = 0,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates    = dynamicStates.data(),
    };

    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineCreateInfo{
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = shaderStages.size(),
        .pStages             = shaderStages.data(),
        .pVertexInputState   = &vertexInputStateCI,
        .pInputAssemblyState = &inputAssemblyStateCI,
        .pViewportState      = &viewportStateCI,
        .pRasterizationState = &rasterizationStateCI,
        .pMultisampleState   = &multiSamplingStateCI,
        .pDepthStencilState  = &depthStencilStateCI,
        .pColorBlendState    = &colorBlendingStateCI,
        .pDynamicState       = &dynamicStateCI,
        .layout              = _pipelineLayout->getHandle(),
        .renderPass          = _renderPass->getHandle(),
        .subpass             = _ci.subPassRef,
        .basePipelineHandle  = VK_NULL_HANDLE,
    };

    VkResult result = vkCreateGraphicsPipelines(_render->getLogicalDevice(),
                                                _render->getPipelineCache(),
                                                1,
                                                &pipelineCreateInfo,
                                                nullptr,
                                                &_pipeline);
    NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create graphics pipeline!");

    // Cleanup shader modules
    vkDestroyShaderModule(_render->getLogicalDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(_render->getLogicalDevice(), vertShaderModule, nullptr);

    NE_CORE_INFO("Vulkan graphics pipeline created successfully: {}  <= {}", (uintptr_t)_pipeline, _ci.shaderCreateInfo.shaderName);
}


// void VulkanPipeline::createDefaultSampler()
// {
//     VkSamplerCreateInfo samplerInfo{
//         .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
//         .magFilter               = VK_FILTER_LINEAR,
//         .minFilter               = VK_FILTER_LINEAR,
//         .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
//         .addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
//         .addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
//         .addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
//         .mipLodBias              = 0.0f,
//         .anisotropyEnable        = VK_TRUE,
//         .maxAnisotropy           = 16.0f,
//         .compareEnable           = VK_FALSE,
//         .compareOp               = VK_COMPARE_OP_ALWAYS,
//         .minLod                  = 0.0f,
//         .maxLod                  = 0.0f,
//         .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
//         .unnormalizedCoordinates = VK_FALSE,
//     };

//     VkResult result = vkCreateSampler(m_logicalDevice, &samplerInfo, nullptr, &m_defaultTextureSampler);
//     NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create texture sampler!");
// }

VkShaderModule VulkanPipeline::createShaderModule(const std::vector<uint32_t> &spv_binary)
{
    VkShaderModuleCreateInfo createInfo{
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext    = nullptr,
        .flags    = 0,
        .codeSize = spv_binary.size() * sizeof(uint32_t),
        .pCode    = spv_binary.data(),
    };

    VkShaderModule shaderModule = nullptr;
    VK_CALL(vkCreateShaderModule(_render->getLogicalDevice(), &createInfo, nullptr, &shaderModule));
    return shaderModule;
}

void VulkanPipeline::queryPhysicalDeviceLimits()
{
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(_render->getPhysicalDevice(), &properties);
    // m_maxTextureSlots = properties.limits.maxPerStageDescriptorSamplers;
}
