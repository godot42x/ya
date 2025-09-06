#include <array>

#include "Core/App/App.h"
#include "Core/Log.h"
#include "VulkanPipeline.h"
#include "VulkanRender.h"
#include "VulkanUtils.h"



void VulkanPipelineLayout::create(
    const std::vector<ya::PushConstantRange>  pushConstants,
    const std::vector<VkDescriptorSetLayout> &layouts)
{
    // _ci = ci;

    std::vector<VkPushConstantRange> vkPSs;
    for (const auto &pushConstant : pushConstants) {
        vkPSs.push_back(VkPushConstantRange{
            .stageFlags = toVk(pushConstant.stageFlags),
            .offset     = pushConstant.offset,
            .size       = pushConstant.size,
        });
    }

    // Create it outside the ci
    // _descriptorSetLayouts.resize(_ci.descriptorSetLayouts.size(), VK_NULL_HANDLE);

    // int i = 0;
    // for (const auto &setLayout : _ci.descriptorSetLayouts) {

    //     std::vector<VkDescriptorSetLayoutBinding> bindings = {};
    //     for (const auto &binding : setLayout.bindings) {
    //         bindings.push_back(VkDescriptorSetLayoutBinding{
    //             .binding            = binding.binding,
    //             .descriptorType     = toVk(binding.descriptorType),
    //             .descriptorCount    = binding.descriptorCount,
    //             .stageFlags         = toVk(binding.stageFlags),
    //             .pImmutableSamplers = nullptr, // TODO: handle immutable samplers
    //         });
    //     }

    //     VkDescriptorSetLayoutCreateInfo dslCI{
    //         .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    //         .pNext        = nullptr,
    //         .flags        = 0,
    //         .bindingCount = static_cast<uint32_t>(bindings.size()),
    //         .pBindings    = bindings.data(),
    //     };

    //     vkCreateDescriptorSetLayout(_render->getLogicalDevice(),
    //                                 &dslCI,
    //                                 _render->getAllocator(),
    //                                 &_descriptorSetLayouts[i]);
    //     ++i;
    // }


    // example for the descriptor set meaning:

    // layout(set=2, binding=0) uniform sampler2D uTexture0; // see comment of SDL_CreateGPUShader, the set is the rule of SDL3!!!

    // layout(set = 3, binding = 0) uniform CameraBuffer{
    //     mat4 model;
    //     mat4 view;
    //     mat4 projection;
    // } uCamera;

    // layout(set = 3, binding = 1) uniform LightBuffer {
    //     vec4 lightDir;
    //     vec4 lightColor;
    //     float ambientIntensity;
    //     float specularPower;
    // } uLight;

    VkPipelineLayoutCreateInfo layoutCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        // .setLayoutCount         = static_cast<uint32_t>(_descriptorSetLayouts.size()),
        // .pSetLayouts            = _descriptorSetLayouts.data(),
        .setLayoutCount         = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts            = layouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(vkPSs.size()),
        .pPushConstantRanges    = vkPSs.data(),
    };

    VkResult result = vkCreatePipelineLayout(_render->getLogicalDevice(), &layoutCI, _render->getAllocator(), &_pipelineLayout);
    if (result != VK_SUCCESS) {
        YA_CORE_ERROR("Failed to create Vulkan pipeline layout: {}", result);
        return; // false
    }

    YA_CORE_INFO("Vulkan pipeline layout created successfully: {}", (uintptr_t)_pipelineLayout);

    _render->setDebugObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, _pipelineLayout, label);
}

void VulkanPipelineLayout::cleanup()
{
    VK_DESTROY(PipelineLayout, _render->getLogicalDevice(), _pipelineLayout);
    // for (auto layout : _descriptorSetLayouts) {
    //     VK_DESTROY(DescriptorSetLayout, _render->getLogicalDevice(), layout);
    // }
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


void VulkanPipeline::createPipelineInternal()
{
    // Process shader
    name = _ci.shaderCreateInfo.shaderName;
    YA_CORE_INFO("Creating pipeline for: {}", name);
    auto shaderStorage = ya::App::get()->getShaderStorage();
    auto stage2Spirv   = shaderStorage->getCache(_ci.shaderCreateInfo.shaderName);
    if (!stage2Spirv) {
        stage2Spirv = shaderStorage->load(_ci.shaderCreateInfo.shaderName);
    }
    YA_CORE_ASSERT(stage2Spirv, "Shader not found in cache: {}", _ci.shaderCreateInfo.shaderName);

    // Create shader modules
    auto vertShaderModule = createShaderModule(stage2Spirv->at(EShaderStage::Vertex));
    auto fragShaderModule = createShaderModule(stage2Spirv->at(EShaderStage::Fragment));

    _render->setDebugObjectName(VK_OBJECT_TYPE_SHADER_MODULE, vertShaderModule, std::format("{}_vert", name).c_str());
    _render->setDebugObjectName(VK_OBJECT_TYPE_SHADER_MODULE, fragShaderModule, std::format("{}_frag", name).c_str());

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
        auto vertexReflectInfo = shaderStorage->getProcessor()->reflect(EShaderStage::Vertex, stage2Spirv->at(EShaderStage::Vertex));

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


    std::vector<VkViewport> viewports;
    for (const auto &viewport : _ci.viewportState.viewports) {
        viewports.push_back({
            .x        = viewport.x,
            .y        = viewport.y,
            .width    = viewport.width,
            .height   = viewport.height,
            .minDepth = viewport.minDepth,
            .maxDepth = viewport.maxDepth,
        });
    }
    std::vector<VkRect2D> scissors;
    for (const auto &scissor : _ci.viewportState.scissors) {
        scissors.push_back({
            .offset = {scissor.offsetX, scissor.offsetY},
            .extent = {scissor.width, scissor.height},
        });
    }
    VkPipelineViewportStateCreateInfo viewportStateCI{
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = 0,
        .viewportCount = static_cast<uint32_t>(viewports.size()),
        .pViewports    = viewports.data(),
        .scissorCount  = static_cast<uint32_t>(scissors.size()),
        .pScissors     = scissors.data(),
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
    // TODO: do not use bitmask enum
    if (_ci.dynamicFeatures & EPipelineDynamicFeature::DepthTest) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE);
    }
    if (_ci.dynamicFeatures & EPipelineDynamicFeature::AlphaBlend) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
    }
    if (_ci.dynamicFeatures & EPipelineDynamicFeature::Viewport) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    }
    if (_ci.dynamicFeatures & EPipelineDynamicFeature::Scissor) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
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
    YA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create graphics pipeline!");

    // Cleanup shader modules
    vkDestroyShaderModule(_render->getLogicalDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(_render->getLogicalDevice(), vertShaderModule, nullptr);

    YA_CORE_TRACE("Vulkan graphics pipeline created successfully: {}  <= {}", (uintptr_t)_pipeline, _ci.shaderCreateInfo.shaderName);

    _render->setDebugObjectName(VK_OBJECT_TYPE_PIPELINE, getHandle(), std::format("Pipeline_{}", name).c_str());
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
//     YA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create texture sampler!");
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
