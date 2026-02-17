#include <array>

#include "Core/App/App.h"
#include "Core/Log.h"
#include "ImGui.h"
#include "Render/RenderDefines.h"
#include "VulkanPipeline.h"
#include "VulkanRender.h"
#include "VulkanUtils.h"




namespace ya
{

void VulkanPipelineLayout::create(const std::vector<PushConstantRange>             pushConstants,
                                  const std::vector<stdptr<IDescriptorSetLayout>>& layouts)
{
    // _ci = ci;

    std::vector<::VkPushConstantRange> vkPSs;
    for (const auto& pushConstant : pushConstants) {
        vkPSs.push_back(::VkPushConstantRange{
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

    //     vkCreateDescriptorSetLayout(_render->getDevice(),
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

    std::vector<VkDescriptorSetLayout> vkLayouts;
    for (const auto& layout : layouts) {
        vkLayouts.push_back(layout->getHandleAs<VkDescriptorSetLayout>());
    }



    VkPipelineLayoutCreateInfo layoutCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        // .setLayoutCount         = static_cast<uint32_t>(_descriptorSetLayouts.size()),
        // .pSetLayouts            = _descriptorSetLayouts.data(),
        .setLayoutCount         = static_cast<uint32_t>(vkLayouts.size()),
        .pSetLayouts            = vkLayouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(vkPSs.size()),
        .pPushConstantRanges    = vkPSs.data(),
    };

    VkResult result = vkCreatePipelineLayout(_render->getDevice(), &layoutCI, _render->getAllocator(), &_pipelineLayout);
    if (result != VK_SUCCESS) {
        YA_CORE_ERROR("Failed to create Vulkan pipeline layout: {}", result);
        return; // false
    }

    YA_CORE_INFO("Vulkan pipeline layout created successfully: {}", (uintptr_t)_pipelineLayout);

    _render->setDebugObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, _pipelineLayout, _label.c_str());
}

void VulkanPipelineLayout::cleanup()
{
    VK_DESTROY(PipelineLayout, _render->getDevice(), _pipelineLayout);
    // for (auto layout : _descriptorSetLayouts) {
    //     VK_DESTROY(DescriptorSetLayout, _render->getDevice(), layout);
    // }
}


void VulkanPipeline::cleanup()
{
    VK_DESTROY(Pipeline, _render->getDevice(), _pipeline);
}

bool VulkanPipeline::recreate(const GraphicsPipelineCreateInfo& ci)
{
    YA_PROFILE_FUNCTION_LOG();
    _ci             = ci;
    // TODO: precreate post create to avoid reentrance?
    _bDirty         = false;
    _pipelineLayout = ci.pipelineLayout->as<VulkanPipelineLayout>();
    try {
        createPipelineInternal();
    }
    catch (const std::exception& e) {
        YA_CORE_ERROR("Failed to create pipeline: {}", e.what());
        return false;
    }
    _render->setDebugObjectName(VK_OBJECT_TYPE_PIPELINE, _pipeline, _name.toString().c_str());
    return true;
}

void VulkanPipeline::reloadShaders(std::optional<GraphicsPipelineCreateInfo> ci)
{
    auto shaderStorage = ya::App::get()->getShaderStorage();
    shaderStorage->removeCache(_ci.shaderDesc.shaderName);
    if (ci.has_value()) {
        _ci = ci.value();
    }
    recreate(_ci);
}

void VulkanPipeline::tryUpdateShader()
{
    if (_pendingNewPipeline) {
        VK_DESTROY(Pipeline, _render->getDevice(), _pipeline);
        _pipeline           = _pendingNewPipeline;
        _pendingNewPipeline = VK_NULL_HANDLE;
        YA_CORE_TRACE("Vulkan graphics pipeline replaced successfully: {}  <= {}", (uintptr_t)_pipeline, _ci.shaderDesc.shaderName);
    }
}

void VulkanPipeline::beginFrame()
{
    if (_bDirty) {
        auto pendingCI = _ci;
        _bDirty        = false;
        if (!recreate(pendingCI)) {
            _bDirty = true;
        }
    }
    tryUpdateShader();
}

void VulkanPipeline::setSampleCount(ESampleCount::T sampleCount)
{
    if (_ci.multisampleState.sampleCount == sampleCount) {
        return;
    }
    _ci.multisampleState.sampleCount = sampleCount;
    _bDirty                          = true;
}

void VulkanPipeline::setCullMode(ECullMode::T cullMode)
{
    if (_ci.rasterizationState.cullMode == cullMode) {
        return;
    }
    _ci.rasterizationState.cullMode = cullMode;
    _bDirty                         = true;
}

void VulkanPipeline::setPolygonMode(EPolygonMode::T polygonMode)
{
    if (_ci.rasterizationState.polygonMode == polygonMode) {
        return;
    }
    _ci.rasterizationState.polygonMode = polygonMode;
    _bDirty                            = true;
}

void VulkanPipeline::renderGUI()
{
    bool bManualReload = false;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.7f, 0.0f, 1.0f));
    if (ImGui::Button("Reload Shaders")) {
        bManualReload = true;
    }
    ImGui::PopStyleColor();
    int cull = static_cast<int>(_ci.rasterizationState.cullMode);
    if (ImGui::Combo("Cull Mode", &cull, "None\0Front\0Back\0FrontAndBack\0")) {
        setCullMode(static_cast<ECullMode::T>(cull));
    }

    int polygonMode = static_cast<int>(_ci.rasterizationState.polygonMode);
    if (ImGui::Combo("Polygon Mode", &polygonMode, "Fill\0Line\0Point\0")) {
        setPolygonMode(static_cast<EPolygonMode::T>(polygonMode));
    }


    // SampleCount need the render pass/render target compatibility,
    //  so we can only allow changing it in a limited scope:
    //  change renderpass(optional) -> change rt/recreate attachments -> change pipeline
    // int sampleCount = toSampleCountIndex(_ci.multisampleState.sampleCount);
    // if (ImGui::Combo("Sample Count", &sampleCount, "1\0"
    //                                                "2\0"
    //                                                "4\0"
    //                                                "8\0"
    //                                                "16\0"
    //                                                "32\0"
    //                                                "64\0")) {
    //     setSampleCount(toSampleCount(sampleCount));
    // }

    if (bManualReload) {
        reloadShaders();
        
    }

}


void VulkanPipeline::createPipelineInternal()
{

    Deleter deleter;

    // Process shader
    _name = _ci.shaderDesc.shaderName;
    YA_CORE_INFO("Creating pipeline for: {}", _name.toString());
    auto shaderStorage = ya::App::get()->getShaderStorage();
    auto stage2Spirv   = shaderStorage->getCache(_ci.shaderDesc.shaderName);
    if (!stage2Spirv) {
        try {

            stage2Spirv = shaderStorage->load(_ci.shaderDesc);
        }
        catch (const std::exception& e) {
            YA_CORE_ERROR("Failed to load shader: {}", e.what());
            return;
        }
        if (!stage2Spirv) {
            YA_CORE_ERROR("Failed to load shader: {}", _ci.shaderDesc.shaderName);
            return;
        }
    }
    YA_CORE_ASSERT(stage2Spirv, "Shader not found in cache: {}", _ci.shaderDesc.shaderName);

    // Create shader modules
    auto vertShaderModule = createShaderModule(stage2Spirv->at(EShaderStage::Vertex));
    auto fragShaderModule = createShaderModule(stage2Spirv->at(EShaderStage::Fragment));
    deleter.push("", vertShaderModule, [this](void* handle) {
        vkDestroyShaderModule(_render->getDevice(), reinterpret_cast<VkShaderModule>(handle), _render->getAllocator());
    });
    deleter.push("", fragShaderModule, [this](void* handle) {
        vkDestroyShaderModule(_render->getDevice(), reinterpret_cast<VkShaderModule>(handle), _render->getAllocator());
    });

    _render->setDebugObjectName(VK_OBJECT_TYPE_SHADER_MODULE, vertShaderModule, std::format("{}_vert", _name.toString()).c_str());
    _render->setDebugObjectName(VK_OBJECT_TYPE_SHADER_MODULE, fragShaderModule, std::format("{}_frag", _name.toString()).c_str());



    std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {
        VkPipelineShaderStageCreateInfo{
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0,
            .stage               = VK_SHADER_STAGE_VERTEX_BIT,
            .module              = vertShaderModule,
            .pName               = "main",
            .pSpecializationInfo = {},
        },
        VkPipelineShaderStageCreateInfo{
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0,
            .stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module              = fragShaderModule,
            .pName               = "main",
            .pSpecializationInfo = {},
        },
    };

    if (stage2Spirv->count(EShaderStage::Geometry)) {
        auto geomShaderModule = createShaderModule(stage2Spirv->at(EShaderStage::Geometry));
        deleter.push("", geomShaderModule, [this](void* handle) {
            vkDestroyShaderModule(_render->getDevice(), reinterpret_cast<VkShaderModule>(handle), _render->getAllocator());
        });
        _render->setDebugObjectName(VK_OBJECT_TYPE_SHADER_MODULE, geomShaderModule, std::format("{}_geom", _name.toString()).c_str());
        shaderStages.push_back(VkPipelineShaderStageCreateInfo{
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0,
            .stage               = VK_SHADER_STAGE_GEOMETRY_BIT,
            .module              = geomShaderModule,
            .pName               = "main",
            .pSpecializationInfo = {},
        });
    }

    // Configure vertex input based on configuration

    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
    std::vector<VkVertexInputBindingDescription>   vertexBindingDescriptions;

    const auto& config = _ci.shaderDesc;

    if (_ci.shaderDesc.bDeriveFromShader) {
        // Get vertex input info from shader reflection
        auto vertexReflectInfo = shaderStorage->getProcessor()->reflect(EShaderStage::Vertex, stage2Spirv->at(EShaderStage::Vertex));

        auto spirvType2VulkanFormat = [](const auto& type) -> VkFormat {
            if (type.vecsize == 3 && type.basetype == 0)
                return VK_FORMAT_R32G32B32_SFLOAT;
            if (type.vecsize == 4 && type.basetype == 0)
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            if (type.vecsize == 2 && type.basetype == 0)
                return VK_FORMAT_R32G32_SFLOAT;
            return VK_FORMAT_R32G32B32_SFLOAT; // Default fallback
        };

        for (auto& input : vertexReflectInfo.inputs) {
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
        for (const auto& bufferDesc : config.vertexBufferDescs) {
            vertexBindingDescriptions.push_back({
                .binding   = bufferDesc.slot,
                .stride    = bufferDesc.pitch,
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            });
        }

        for (const auto& attr : config.vertexAttributes) {
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
    for (const auto& viewport : _ci.viewportState.viewports) {
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
    for (const auto& scissor : _ci.viewportState.scissors) {
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
    for (const auto& attachment : _ci.colorBlendState.attachments) {
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
    std::ranges::transform(
        _ci.dynamicFeatures,
        std::back_inserter(dynamicStates),
        [](EPipelineDynamicFeature::T feature) {
            switch (feature) {
            case EPipelineDynamicFeature::DepthTest:
                return VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
            case EPipelineDynamicFeature::BlendConstants:
                return VK_DYNAMIC_STATE_BLEND_CONSTANTS;
            case EPipelineDynamicFeature::Viewport:
                return VK_DYNAMIC_STATE_VIEWPORT;
            case EPipelineDynamicFeature::Scissor:
                return VK_DYNAMIC_STATE_SCISSOR;
            case EPipelineDynamicFeature::CullMode:
                return VK_DYNAMIC_STATE_CULL_MODE;
            case EPipelineDynamicFeature::PolygonMode:
                return VK_DYNAMIC_STATE_POLYGON_MODE_EXT;
            default:
                return VK_DYNAMIC_STATE_MAX_ENUM; // Invalid, will be filtered out
            }
        });

    VkPipelineDynamicStateCreateInfo dynamicStateCI{
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext             = nullptr,
        .flags             = 0,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates    = dynamicStates.data(),
    };


    // Create pipeline
    VkGraphicsPipelineCreateInfo gplCI{
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = nullptr,
        .flags               = 0,
        .stageCount          = static_cast<uint32_t>(shaderStages.size()),
        .pStages             = shaderStages.data(),
        .pVertexInputState   = &vertexInputStateCI,
        .pInputAssemblyState = &inputAssemblyStateCI,
        .pViewportState      = &viewportStateCI,
        .pRasterizationState = &rasterizationStateCI,
        .pMultisampleState   = &multiSamplingStateCI,
        .pDepthStencilState  = &depthStencilStateCI,
        .pColorBlendState    = &colorBlendingStateCI,
        .pDynamicState       = &dynamicStateCI,
        .layout              = _pipelineLayout->getVkHandle(),
        .renderPass          = VK_NULL_HANDLE,
        .subpass             = 0,
        .basePipelineHandle  = VK_NULL_HANDLE,
    };

    if (_ci.renderPass != nullptr)
    {
        // 传统流程需设置 renderPass 和 subpass，动态渲染模式下这两个参数设为 VK_NULL_HANDLE 与 0
        gplCI.renderPass = _ci.renderPass->getHandleAs<VkRenderPass>();
        gplCI.subpass    = _ci.subPassRef;
    }
    else
    {
        // WHY DELETER? RAII will destruct outside this case, extend this life cycle until created pipeline done
        auto colorAttachmentRef = deleter.push("", new std::vector<VkFormat>(), [](void* handle) {
            if (handle) {
                delete static_cast<std::vector<VkFormat>*>(handle);
            }
        });
        YA_CORE_ASSERT(_ci.pipelineRenderingInfo.colorAttachmentFormats.size() > 0, "Not a valid dyn rendering pipeline creation info");
        std::ranges::transform(
            _ci.pipelineRenderingInfo.colorAttachmentFormats,
            std::back_inserter(*colorAttachmentRef),
            [](EFormat::T format) {
                return toVk(format);
            });

        // auto plRI = makeShared<VkPipelineRenderingCreateInfo>(VkPipelineRenderingCreateInfo{
        auto plRI = new VkPipelineRenderingCreateInfo(VkPipelineRenderingCreateInfo{
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext                   = nullptr,
            .viewMask                = 0,
            .colorAttachmentCount    = static_cast<uint32_t>(_ci.pipelineRenderingInfo.colorAttachmentFormats.size()),
            .pColorAttachmentFormats = colorAttachmentRef->data(),
            .depthAttachmentFormat   = toVk(_ci.pipelineRenderingInfo.depthAttachmentFormat),
            .stencilAttachmentFormat = toVk(_ci.pipelineRenderingInfo.stencilAttachmentFormat),
        });
        // auto ref  = deleter.add(plRI, nullptr);
        auto ref = deleter.push("", plRI, [](void* handle) {
            delete static_cast<VkPipelineRenderingCreateInfo*>(handle);
        });

        // 关键：将动态渲染配置挂到 gplCI 的 pNext 上
        gplCI.pNext = ref;
        // gplCI.
    }


    VkPipeline newPipeline = VK_NULL_HANDLE;

    VkResult result = vkCreateGraphicsPipelines(_render->getDevice(),
                                                _render->getPipelineCache(),
                                                1,
                                                &gplCI,
                                                _render->getAllocator(),
                                                &newPipeline);
    YA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create graphics pipeline!");
    YA_CORE_ASSERT(newPipeline != VK_NULL_HANDLE, "Failed to create graphics pipeline!");

    // Destroy old pipeline
    if (!_pipeline && !_pendingNewPipeline) {
        _pipeline = newPipeline;
    }
    else {
        _pendingNewPipeline = newPipeline;
    }

    YA_CORE_TRACE("Vulkan graphics pipeline created successfully: {}  <= {}", (uintptr_t)_pipeline, _ci.shaderDesc.shaderName);

    _render->setDebugObjectName(VK_OBJECT_TYPE_PIPELINE, getVkHandle(), std::format("Pipeline_{}", _name.toString()).c_str());
}



VkShaderModule VulkanPipeline::createShaderModule(const std::vector<uint32_t>& spv_binary)
{
    VkShaderModuleCreateInfo createInfo{
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext    = nullptr,
        .flags    = 0,
        .codeSize = spv_binary.size() * sizeof(uint32_t),
        .pCode    = spv_binary.data(),
    };

    VkShaderModule shaderModule = nullptr;
    VK_CALL(vkCreateShaderModule(_render->getDevice(), &createInfo, nullptr, &shaderModule));
    return shaderModule;
}

void VulkanPipeline::queryPhysicalDeviceLimits()
{
    ::VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(_render->getPhysicalDevice(), &properties);
    // m_maxTextureSlots = properties.limits.maxPerStageDescriptorSamplers;
}

} // namespace ya