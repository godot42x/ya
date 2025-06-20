#include "VulkanDevice.h"

#include <Core/Base.h>
#include <Core/Log.h>

#include <Render/Shader.h>
#include <array>
#include <cstring>
#include <set>
#include <unordered_map>


#define panic(...) NE_CORE_ASSERT(false, __VA_ARGS__);



VkSurfaceFormatKHR SwapChainSupportDetails::ChooseSwapSurfaceFormat()
{
    // Method 1
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
    {
        return {
            .format     = VK_FORMAT_B8G8R8A8_UNORM,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        };
    }

    // Method 2
    for (const auto &available_format : formats)
    {
        if (available_format.format == VK_FORMAT_B8G8R8A8_UNORM && available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return available_format;
        }
    }

    // Fallback, ??
    return formats[0];
}

VkPresentModeKHR SwapChainSupportDetails::ChooseSwapPresentMode()
{

    for (const auto &available_present_mode : present_modes)
    {
        if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return available_present_mode;
        }
        else if (available_present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            return available_present_mode;
        }
    }

    // fallback best mode
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapChainSupportDetails::ChooseSwapExtent(WindowProvider *provider)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width, height;
    provider->getWindowSize(width, height);
    VkExtent2D actual_extent = {(uint32_t)width, (uint32_t)height};

    actual_extent.width = std::max(
        capabilities.minImageExtent.width,
        std::min(capabilities.maxImageExtent.width, actual_extent.width));
    actual_extent.height = std::max(
        capabilities.minImageExtent.height,
        std::min(capabilities.maxImageExtent.height, actual_extent.height));

    return actual_extent;
}

SwapChainSupportDetails SwapChainSupportDetails::query(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    SwapChainSupportDetails details;

    // Capabilities
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    // Formats
    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
    if (format_count != 0)
    {
        details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats.data());
    }

    // PresentModes
    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);
    if (present_mode_count != 0)
    {
        details.present_modes.resize(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, details.present_modes.data());
    }

    return details;
}


void VulkanState::create_instance()
{
    if (m_EnableValidationLayers && !is_validation_layers_supported()) {
        NE_CORE_ASSERT(false, "validation layers requested, but not available!");
    }

    VkApplicationInfo app_info{
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = "Neon Engine",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "No Engine",
        .apiVersion         = VK_API_VERSION_1_2,
    };

    std::vector<const char *> extensions = onGetRequiredExtensions.ExecuteIfBound();
    if (m_EnableValidationLayers)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo instance_create_info{
        .sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .pApplicationInfo = &app_info,
        // .enabledLayerCount       = 0,
        // .ppEnabledLayerNames     = nullptr,
        .enabledExtensionCount   = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    if (m_EnableValidationLayers) {
        const VkDebugUtilsMessengerCreateInfoEXT &debug_messenger_create_info = get_debug_messenger_create_info_ext();

        instance_create_info.enabledLayerCount   = static_cast<uint32_t>(m_ValidationLayers.size());
        instance_create_info.ppEnabledLayerNames = m_ValidationLayers.data();
        instance_create_info.pNext               = (VkDebugUtilsMessengerCreateInfoEXT *)&debug_messenger_create_info;
    }
    else {
        instance_create_info.enabledLayerCount = 0;
        instance_create_info.pNext             = nullptr;
    }

    VkResult result = vkCreateInstance(&instance_create_info, nullptr, &m_Instance);
    if (result != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to create instance!");
    }
}


// Assign the m_physicalDevice, m_PresentQueue, m_GraphicsQueue
void VulkanState::create_logic_device()
{
    QueueFamilyIndices family_indices = QueueFamilyIndices::query(m_Surface, m_PhysicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queue_crate_infos;

    float queue_priority = 1.0f;
    for (int queue_family : {family_indices.graphics_family, family_indices.supported_family}) {
        queue_crate_infos.push_back({
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .queueFamilyIndex = static_cast<uint32_t>(queue_family),
            .queueCount       = 1,
            .pQueuePriorities = &queue_priority,
        });
    }

    VkPhysicalDeviceFeatures device_features{
        .samplerAnisotropy = VK_TRUE,
    };

    VkDeviceCreateInfo deviceCreateInfo = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .queueCreateInfoCount    = static_cast<uint32_t>(queue_crate_infos.size()),
        .pQueueCreateInfos       = queue_crate_infos.data(),
        .enabledLayerCount       = 0,
        .ppEnabledLayerNames     = nullptr,
        .enabledExtensionCount   = static_cast<uint32_t>(m_DeviceExtensions.size()),
        .ppEnabledExtensionNames = m_DeviceExtensions.data(),
        .pEnabledFeatures        = &device_features

    };
    if (m_EnableValidationLayers)
    {
        deviceCreateInfo.enabledLayerCount   = static_cast<uint32_t>(m_ValidationLayers.size());
        deviceCreateInfo.ppEnabledLayerNames = m_ValidationLayers.data();
    }


    VkResult ret = vkCreateDevice(m_PhysicalDevice, &deviceCreateInfo, nullptr, &m_LogicalDevice);
    NE_ASSERT(ret == VK_SUCCESS, "failed to create logical device!");

    vkGetDeviceQueue(m_LogicalDevice, family_indices.supported_family, 0, &m_PresentQueue);
    vkGetDeviceQueue(m_LogicalDevice, family_indices.graphics_family, 0, &m_GraphicsQueue);
    NE_ASSERT(m_PresentQueue != VK_NULL_HANDLE, "failed to get present queue!");
    NE_ASSERT(m_GraphicsQueue != VK_NULL_HANDLE, "failed to get graphics queue!");
}


void VulkanState::create_swapchain()
{
    // Swap chain support details
    SwapChainSupportDetails supportDetails = SwapChainSupportDetails::query(m_PhysicalDevice, m_Surface);

    VkSurfaceFormatKHR surface_format = supportDetails.ChooseSwapSurfaceFormat();
    VkPresentModeKHR   presentMode    = supportDetails.ChooseSwapPresentMode();

    m_SwapChainExtent      = supportDetails.ChooseSwapExtent(_windowProvider);
    m_SwapChainImageFormat = surface_format.format;


    uint32_t image_count = supportDetails.capabilities.minImageCount + 1;
    if (supportDetails.capabilities.maxImageCount > 0 &&
        image_count > supportDetails.capabilities.maxImageCount)
    {
        image_count = supportDetails.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_create_info{
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = m_Surface,
        .minImageCount    = image_count,
        .imageFormat      = m_SwapChainImageFormat,
        .imageColorSpace  = surface_format.colorSpace,
        .imageExtent      = m_SwapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform     = supportDetails.capabilities.currentTransform, // No transform op
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = presentMode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = VK_NULL_HANDLE,
    };
    // TODO: only need to query once? no high performance required
    QueueFamilyIndices indices = QueueFamilyIndices::query(m_Surface, m_PhysicalDevice);

    uint32_t queue_family_indices[] = {
        (uint32_t)indices.graphics_family,
        (uint32_t)indices.supported_family,
    };

    // Q: what's the difference between concurrent and exclusive sharing
    // AI: Concurrent sharing allows multiple queues to access the same image simultaneously,
    //  while exclusive sharing allows only one queue to access the image at a time.

    if (indices.graphics_family != indices.supported_family)
    {
        swapchain_create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        swapchain_create_info.queueFamilyIndexCount = 2;
        swapchain_create_info.pQueueFamilyIndices   = queue_family_indices;
    }
    else {
        swapchain_create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_create_info.queueFamilyIndexCount = 0;       // Optional
        swapchain_create_info.pQueueFamilyIndices   = nullptr; // Optional
    }


    // Create swapchain
    VkResult ret = vkCreateSwapchainKHR(m_LogicalDevice, &swapchain_create_info, nullptr, &m_SwapChain);
    NE_CORE_ASSERT(ret == VK_SUCCESS, "failed to create swapchain!");
    NE_ASSERT(m_SwapChain != VK_NULL_HANDLE, "failed to create swapchain!");
}

void VulkanState::init_swapchain_images()
{
    uint32_t image_count;
    vkGetSwapchainImagesKHR(m_LogicalDevice, m_SwapChain, &image_count, nullptr);
    m_SwapChainImages.resize(image_count);
    vkGetSwapchainImagesKHR(m_LogicalDevice, m_SwapChain, &image_count, m_SwapChainImages.data());
}

void VulkanState::create_iamge_views()
{
    m_SwapChainImageViews.resize(m_SwapChainImages.size());

    for (size_t i = 0; i < m_SwapChainImages.size(); ++i)
    {
        m_SwapChainImageViews[i] = helper.createImageView(m_SwapChainImages[i],
                                                          m_SwapChainImageFormat,
                                                          VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void VulkanState::create_descriptor_set_layout()
{
    // UBO layout
    VkDescriptorSetLayoutBinding ubo_layout_binding = {
        .binding            = 0,
        .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount    = 1,
        .stageFlags         = VK_SHADER_STAGE_VERTEX_BIT, // TODO: can | a fragment bit
        .pImmutableSamplers = nullptr,                    // Optional
    };

    // sampler layout
    VkDescriptorSetLayoutBinding sampler_layout_binding = {
        .binding            = 1,
        .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount    = 1,
        .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr,
    };


    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
        ubo_layout_binding,
        sampler_layout_binding,
    };

    VkDescriptorSetLayoutCreateInfo layout_crate_info =
        {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings    = bindings.data(),
        };
    if (VK_SUCCESS != vkCreateDescriptorSetLayout(m_LogicalDevice,
                                                  &layout_crate_info,
                                                  nullptr,
                                                  &m_descriptorSetLayout)) {
        NE_CORE_ASSERT(false, "failed to create descriptor set layout");
    }
    NE_ASSERT(m_descriptorSetLayout, "failed to create descriptor set layout");
}

void VulkanState::createGraphicsPipeline(std::unordered_map<EShaderStage::T, std::vector<uint32_t>> spv_binaries)
{
    /************************** Shader Stages ***********************************************/

    /* Shader modules define the programmable pipeline stages */


    // GLSLScriptProcessor                                     processor("engine/shaders/default.glsl");
    // bool                                                    Ok = processor.TakeSpv(spv_binaries);
    // NE_ASSERT(Ok, "failed to take spv binaries");

    // Compile Module (Code)
    auto vertShaderModule = create_shader_module(spv_binaries[EShaderStage::Vertex]);
    auto fragShaderModule = create_shader_module(spv_binaries[EShaderStage::Fragment]);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertShaderModule,
        .pName  = "main",
    };
    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragShaderModule,
        .pName  = "main",
    };

    // Shader Stage createInfo
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // VertexInput v_input(0);
    // v_input.AddAttribute(VK_FORMAT_R32G32B32_SFLOAT, "postion")
    //     .AddAttribute(VK_FORMAT_R32G32B32_SFLOAT, "color")
    //     .AddAttribute(VK_FORMAT_R32G32_SFLOAT, "texture_coord");

    // the description of vertex(declaration) should compatible with the  shader module
    // auto bindingDescription    = v_input.GetBindingDescription();
    // auto attributeDescriptions = v_input.GetAttributeDescriptions();

    // vertex input State createInfo
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        //     .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        //     .vertexBindingDescriptionCount   = 1,
        //     .pVertexBindingDescriptions      = &bindingDescription,
        //     .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        //     .pVertexAttributeDescriptions    = attributeDescriptions.data(),
    };
    /************************** Shader Stages ***********************************************/


    /****************************** Fix-function State *****************************************/


    // Assembly state creatInfo ( Triangle format in this example)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    {
        inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;
    }

    // Viewport defines the transformation mapping from image to framebuffer region, corresponding to xy coordinates and hw dimensions, minmax
    VkViewport viewport = {};
    {
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = (float)m_SwapChainExtent.width;
        viewport.height   = (float)m_SwapChainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
    }

    // Scissor clipping defines which pixels will be discarded
    VkRect2D scissor = {};
    {
        scissor.offset = {0, 0};
        scissor.extent = m_SwapChainExtent; // Keep the clipping region consistent
    }

    // Viewport State createInfo combined with clipping regions and viewports
    VkPipelineViewportStateCreateInfo viewportState = {};
    {
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports    = &viewport;
        viewportState.scissorCount  = 1;
        viewportState.pScissors     = &scissor;
    }


    // Rasterization State createInfo handles geometry transmission to fragment shader, including depth testing, scissor and clipping tests, wireframe or filled primitive rendering
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    {
        rasterizer.sType            = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;

        // Enable depth clamping and discard primitives that exceed the near/far planes, affecting image rendering, requires GPU support; otherwise enable primitive discard beyond framebuffer
        rasterizer.rasterizerDiscardEnable = VK_FALSE; // polygonMode controls how geometry is filled. Available modes:
        // VK_POLYGON_MODE_FILL: fill polygons with fragments
        // VK_POLYGON_MODE_LINE : draw polygon edges as lines
        // VK_POLYGON_MODE_POINT : draw polygon vertices as points
        // Using any mode other than fill requires enabling a GPU feature.
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;

        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode  = VK_CULL_MODE_BACK_BIT; // culling/font faces/call back faces/all ��ü��ķ�ʽ
        // rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // ˳ʱ��/��ʱ�� �������˳��
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; /// ��ת y ��������ʱ�����

        rasterizer.depthBiasEnable         = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // Optional
        rasterizer.depthBiasClamp          = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor    = 0.0f; // Optional
    }

    // Multisample State createInfo ���ز��� ���ﲻʹ��
    VkPipelineMultisampleStateCreateInfo multisample_state_create_info{
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable   = VK_FALSE,
        .minSampleShading      = 1.0f,     // Optional
        .pSampleMask           = nullptr,  // Optional
        .alphaToCoverageEnable = VK_FALSE, // Optional
        .alphaToOneEnable      = VK_FALSE, // Optional
    };

    // Blender ��fragment �������ɫ��ɵ� framebuffer �д��ڵ���ɫ���л�ϣ� ��ϣ��������Ϊ�µ���ɫ���� λ���� , ������õ�һ�ַ�ʽ
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    {
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable    = VK_FALSE;

        if (colorBlendAttachment.blendEnable) {
            // finalcolor = newcolor & color writemask
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;      // Optional
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
            colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;      // Optional
        }
        else {
            // alpha blending: finalcolor.rgb = newAlpha*newColor + (1-newAlpha)*oldColor; finalcolor.a = newAlpha.a
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; // blend method
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
    }

    // color blend createInfo
    VkPipelineColorBlendStateCreateInfo colorBlendingCreateInfo = {};
    {
        colorBlendingCreateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendingCreateInfo.logicOpEnable     = VK_FALSE; // ���õڶ��ַ�ʽ��Ҫ Ϊ true, �����Ʋ����� logicOp �ֶ���ָ��
        colorBlendingCreateInfo.attachmentCount   = 1;
        colorBlendingCreateInfo.pAttachments      = &colorBlendAttachment;
        colorBlendingCreateInfo.blendConstants[0] = 0.0f; // Optional
        colorBlendingCreateInfo.blendConstants[1] = 0.0f; // Optional
        colorBlendingCreateInfo.blendConstants[2] = 0.0f; // Optional
        colorBlendingCreateInfo.blendConstants[3] = 0.0f; // Optional
    }

    // ����ʱ��̬�޸�
    VkDynamicState dynamicState[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_LINE_WIDTH};
    VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {};
    {
        dynamicStateCreateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateCreateInfo.dynamicStateCount = 2; // same to above
        dynamicStateCreateInfo.pDynamicStates    = dynamicState;
    }

    // ��Ȳ��������ͼ
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    {
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        depthStencil.depthTestEnable  = VK_TRUE; // �Ƿ��µ�depth����ȥ��ɵıȽϣ���ȷ���Ƿ���
        depthStencil.depthWriteEnable = VK_TRUE; // �µ� depth buffer �Ƿ�ʵ��д��

        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS; // ��������Ƭ�εıȽ�ϸ�ڣ�less��ζ�Ž����µ����ֵӦ�ø���

        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front             = {}; // Optional
        depthStencil.back              = {}; // Optional
    }

    /********************************* Fixed-function State ********************************************/


    /************************************* Pipeline Layout ********************************************/

    /* Pipeline layout defines uniform variables and push constants that can be accessed by shaders during each drawing call */

    // pipeLayout Createinfo
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
    {
        pipelineLayoutCreateInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.setLayoutCount         = 1;
        pipelineLayoutCreateInfo.pSetLayouts            = &m_descriptorSetLayout; // ʹ�ó�Ա�������е�layout(createDescriptorSetLayout())
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;                      // Optional
        pipelineLayoutCreateInfo.pPushConstantRanges    = 0;                      // Optional
    }
    if (vkCreatePipelineLayout(m_LogicalDevice, &pipelineLayoutCreateInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "filed to create pipeline layout!");
    }
    /************************************* Pipeline Layout ********************************************/
    /************************************* Render Pass ********************************************/
    /*   NOTE: createRenderPass() moved to VulkanRenderPass class          */
    /************************************* Render Pass ********************************************/


    // Pipeline createInfo
    VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
    {
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

        // pStages pointer to state create info array
        pipelineCreateInfo.stageCount = 2; // vertex AND fragment State
        pipelineCreateInfo.pStages    = shaderStages;

        pipelineCreateInfo.pVertexInputState   = &vertexInputInfo;
        pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
        pipelineCreateInfo.pViewportState      = &viewportState;
        pipelineCreateInfo.pRasterizationState = &rasterizer;
        pipelineCreateInfo.pMultisampleState   = &multisample_state_create_info;
        pipelineCreateInfo.pDepthStencilState  = nullptr; // Depth  Optional
        pipelineCreateInfo.pColorBlendState    = &colorBlendingCreateInfo;
        pipelineCreateInfo.pDynamicState       = nullptr; // Dynamicstage Optional

        pipelineCreateInfo.layout     = m_pipelineLayout;
        pipelineCreateInfo.renderPass = m_renderPass.getRenderPass(); // bind renderPass
        pipelineCreateInfo.subpass    = 0;                            // subpass index

        /*
        Pipeline derivation allows creating new graphics pipelines based on existing ones.
        This can reduce creation time when pipelines share common state and may allow
        driver optimizations and better cache usage.
        You can specify base pipeline using basePipelineHandle or basePipelineIndex.
        Currently we only have one pipeline, so we just set these to null/invalid values.
        These values are only used when VK_PIPELINE_CREATE_DERIVATIVE_BIT flag is set.
        */
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE; //  Optional
        pipelineCreateInfo.basePipelineIndex  = -1;             //  Optional

        pipelineCreateInfo.pDepthStencilState = &depthStencil;
    } // 1. Create graphics pipeline and save to member variable, can later reference multiple pipelineCreateInfo objects
    // 2. Second parameter is pipeline cache for storing and reusing data across multiple calls to VkCreateGraphicsPipelines,
    //    can be saved to file and loaded later to speed up pipeline creation
    if (vkCreateGraphicsPipelines(m_LogicalDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_graphicsPipeLine) != VK_SUCCESS)
    {
        panic("failed to create graphics pipeline!");
    }


    // Destroy shaders modules to release resources after complete createPipeline
    vkDestroyShaderModule(m_LogicalDevice, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_LogicalDevice, vertShaderModule, nullptr);
}

void VulkanState::createCommandPool()
{

    QueueFamilyIndices queueFamilyIndices = QueueFamilyIndices::query(m_Surface, m_PhysicalDevice);

    VkCommandPoolCreateInfo poolInfo = {};
    {
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphics_family;
        poolInfo.flags            = 0;
    }

    if (vkCreateCommandPool(m_LogicalDevice, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        panic("failed to create commandPool!");
    }
}

void VulkanState::createDepthResources()
{

    auto findSupportedFormat = [this](const std::vector<VkFormat> &candidates,
                                      VkImageTiling                tiling,
                                      VkFormatFeatureFlags         features) {
        for (VkFormat format : candidates) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
                return format;
            }
            if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }

        NE_CORE_ASSERT(false, "failed to find supported format!");
        return VK_FORMAT_UNDEFINED;
    };


    auto findDepthFormat = [findSupportedFormat]() {
    };

    VkFormat depthFormat = findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);


    helper.createImage(m_SwapChainExtent.width,
                       m_SwapChainExtent.height,
                       depthFormat,
                       VK_IMAGE_TILING_OPTIMAL,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       m_depthImage,
                       m_depthImageMemory);

    m_depthImageView = helper.createImageView(m_depthImage,
                                              depthFormat,
                                              VK_IMAGE_ASPECT_DEPTH_BIT);

    helper.transitionImageLayout(m_depthImage,
                                 depthFormat,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}


void VulkanState::createTextureSampler()
{
    VkSamplerCreateInfo samplerInfo = {};
    {
        samplerInfo.sType     = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;

        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy    = 16;

        samplerInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp     = VK_COMPARE_OP_ALWAYS;

        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod     = 0.0f;
        samplerInfo.maxLod     = 0.0f;
    }

    if (vkCreateSampler(m_LogicalDevice, &samplerInfo, nullptr, &m_defaultTextureSampler) != VK_SUCCESS)
    {
        panic("failed to create texture sampler!");
    }
}

// void VulkanState::loadModel()
// {
//     // tinyobj::attrib_t                attrib;
//     // std::vector<tinyobj::shape_t>    shapes; // �������е����Ķ������
//     // std::vector<tinyobj::material_t> material;
//     // std::string                      warn;
//     // std::string                      err;

//     // if (!tinyobj::LoadObj(&attrib, &shapes, &material, &warn, &err, MODEL_PATH.c_str()))
//     // {
//     //     panic(err);
//     // }

//     // for (const auto &shape : shapes)
//     // {
//     //     for (const auto &index : shape.mesh.indices)
//     //     {
//     //         Vertex vertex = {};
//     //         vertex.pos    = {
//     //             attrib.vertices[3 * index.vertex_index + 0], // �� float ���� *3
//     //             attrib.vertices[3 * index.vertex_index + 1],
//     //             attrib.vertices[3 * index.vertex_index + 2]};
//     //         vertex.texCoord = {
//     //             attrib.texcoords[2 * index.texcoord_index + 0],
//     //             1.0f - attrib.texcoords[2 * index.texcoord_index + 1]};

//     //         vertex.color = {1.0f, 1.0f, 1.0f};


//     //         m_vertices.push_back(vertex);
//     //         m_indices.push_back(m_indices.size());
//     //     }
//     // }
// }

void VulkanState::createVertexBuffer(void *data, std::size_t size, VkBuffer &outVertexBuffer, VkDeviceMemory &outVertexBufferMemory)
{

    VkDeviceSize buffersize = size;


    VkBuffer       stagingBuffer; // aka transfer buffer?
    VkDeviceMemory stagingBufferMemory;
    helper.createBuffer(buffersize,
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        stagingBuffer,
                        stagingBufferMemory);



    void *gpuData;
    vkMapMemory(m_LogicalDevice, stagingBufferMemory, 0, buffersize, 0, &gpuData); // map ����ʱ�ڴ�
    std::memcpy(gpuData, data, (size_t)buffersize);
    vkUnmapMemory(m_LogicalDevice, stagingBufferMemory); // ֹͣӳ��

    helper.createBuffer(buffersize,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        outVertexBuffer,
                        outVertexBufferMemory);


    helper.copyBuffer(stagingBuffer, outVertexBuffer, buffersize); // encapsulation ��װ copy ����


    // ������ʹ����� stagingBuffer �� stagingBufferMemory    vkDestroyBuffer(m_LogicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(m_LogicalDevice, stagingBufferMemory, nullptr);
}

// void VulkanState::createIndexBuffer()
// {
//     // ������vertex buffer
//     VkDeviceSize bufferSize = sizeof(m_indices[0]) * m_indices.size();

//     VkBuffer       stagingBuffer;
//     VkDeviceMemory stagingBufferMemory;
//     helper.createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

//     void *data;
//     vkMapMemory(m_LogicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
//     memcpy(data, m_indices.data(), (size_t)bufferSize);
//     vkUnmapMemory(m_LogicalDevice, stagingBufferMemory);

//     helper.createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indexBuffer, m_indexBufferMemory);

//     helper.copyBuffer(stagingBuffer, m_indexBuffer, bufferSize);


//     // clean stage data
//     vkDestroyBuffer(m_LogicalDevice, stagingBuffer, nullptr);
//     vkFreeMemory(m_LogicalDevice, stagingBufferMemory, nullptr);
// }

// void VulkanState::createUniformBuffer(uint32_t size)
// {
//     V

//     helper.createBuffer(size,
//                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
//                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
//                         m_uniformBuffer,
//                         m_uniformBUfferMemory);
// }

void VulkanState::createDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 2> poolSizes = {};
    {
        poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = 1;

        poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = 1;
    }

    // layout(set = 1, binding = 0) uniform UniformBufferObject
    // layout(set = 1, binding = 1) uniform sampler2D textureSampler;
    VkDescriptorPoolCreateInfo poolInfo = {};
    {
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes    = poolSizes.data();
        poolInfo.maxSets       = 1;
    }

    if (vkCreateDescriptorPool(m_LogicalDevice, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
    {
        panic("failed to create descripor pool!");
    }
}

// like the sampler, it's a resource of the device,
// should the render pass compatible with the descriptor set layout?
void VulkanState::createDescriptorSet()
{
    VkDescriptorSetLayout       layouts[] = {m_descriptorSetLayout};
    VkDescriptorSetAllocateInfo allocInfo = {};
    {
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = layouts;
    }
    if (vkAllocateDescriptorSets(m_LogicalDevice, &allocInfo, &m_DescriptorSet))
    {
        panic("failed to allocate descriptor set!");
    } // Configure internal descriptor sets - need to update buffer info
    // Set UBO layout information
    VkDescriptorBufferInfo bufferInfo = {};
    {
        // bufferInfo.buffer = m_uniformBuffer;
        // bufferInfo.offset = 0;
        // bufferInfo.range  = sizeof(UniformBufferObject);
    }

    // Q: why a simpler need a image view?
    // AI: The image view is necessary to provide a way for the shader to access the texture data.

    // vec4 color = texture(textureSampler, texCoord);
    // 即一个 sampler 绑定一个 image view, 但 sampler 可以重用
    // std::vector<VkDescriptorImageInfo> imageInfos{
    //     VkDescriptorImageInfo{
    //         .sampler     = m_defaultTextureSampler,
    //         .imageView   = m_textureImageView, // 这个要对哪个image做sample
    //         .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    //     } //  Optional
    // };


    // layout(set = 1, binding = 0) uniform UniformBufferObject
    // layout(set = 1, binding = 1) uniform sampler2D textureSampler;
    std::array<VkWriteDescriptorSet, 2> descriptor_writes = {
        VkWriteDescriptorSet{
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet           = m_DescriptorSet,
            .dstBinding       = 0,
            .dstArrayElement  = 0,
            .descriptorCount  = 1,
            .descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pImageInfo       = nullptr,     //  Optional
            .pBufferInfo      = &bufferInfo, // set buffer info - choose pBufferInfo for buffer descriptor
            .pTexelBufferView = nullptr,     // Optional
        },
        {
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet           = m_DescriptorSet,
            .dstBinding       = 1,
            .dstArrayElement  = 0,
            .descriptorCount  = 0,
            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo       = nullptr, //&image_info, // ����ѡ�� pImage
            .pBufferInfo      = nullptr,
            .pTexelBufferView = nullptr,
        }};

    vkUpdateDescriptorSets(m_LogicalDevice,
                           static_cast<uint32_t>(descriptor_writes.size()),
                           descriptor_writes.data(),
                           0,
                           nullptr);
}

void VulkanState::createCommandBuffers()
{
    m_commandBuffers.resize(m_renderPass.getFramebuffers().size());

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
    {
        commandBufferAllocateInfo.sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocateInfo.commandPool = m_commandPool;
        commandBufferAllocateInfo.level       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        // Question: what's VK_COMMAND_BUFFER_LEVEL_PRIMARY/SECONDARY?
        // AI:
        // In Vulkan, command buffers can be allocated at two levels:
        // 1. Primary Command Buffers: These are the main command buffers that can be submitted to a queue for execution.
        // They can contain any commands, including drawing commands, compute commands, and more.
        // They are typically used for rendering operations.
        // 2. Secondary Command Buffers:
        // These are command buffers that can be recorded with commands but cannot be submitted directly.
        // Instead, they are intended to be called from primary command buffers.
        // They are useful for organizing command buffers and reusing command sequences.
        // 不能直接提交到队列
        // 必须通过主命令缓冲区的 vkCmdExecuteCommands 调用
        // 不能开始/结束渲染通道（但可以在渲染通道内执行）
        // 可以被多个主命令缓冲区重复使用
        // 多线程命令记录，可重用的绘制命令序列

        commandBufferAllocateInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();
    }
    if (vkAllocateCommandBuffers(m_LogicalDevice, &commandBufferAllocateInfo, m_commandBuffers.data()) != VK_SUCCESS)
    {
        panic("failed to allocate command buffers!");
    }
}

void VulkanState::createSemaphores()
{
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &m_imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &m_renderFinishedSemaphore) != VK_SUCCESS)
    {
        panic("failed to create semaphores!");
    }
}

void VulkanState::recreateSwapChain()
{
    vkDeviceWaitIdle(m_LogicalDevice); // ����������ʹ���е���Դ

    create_swapchain();
    init_swapchain_images();
    create_iamge_views();

    // Recreate render pass and framebuffers
    createDepthResources();
    m_renderPass.recreate(m_SwapChainImageViews, m_depthImageView, m_SwapChainExtent);
    createCommandBuffers();
}

// A physical device is suitable/supported for ...
bool VulkanState::is_device_suitable(VkPhysicalDevice device)
{
    // VkPhysicalDeviceProperties deviceProperties;
    // vkGetPhysicalDeviceProperties(device, &deviceProperties);

    // VkPhysicalDeviceFeatures devicesFeatures;
    // vkGetPhysicalDeviceFeatures(device, &devicesFeatures);

    // return (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) && devicesFeatures.geometryShader;

    QueueFamilyIndices indices = QueueFamilyIndices::query(
        m_Surface, device, VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT);

    bool bExtensionSupported = is_device_extension_support(device);

    bool bSwapchainComplete = false;
    if (bExtensionSupported)
    {
        SwapChainSupportDetails swapchain_support_details = SwapChainSupportDetails::query(device, m_Surface);

        bSwapchainComplete = !swapchain_support_details.formats.empty() &&
                             !swapchain_support_details.present_modes.empty();
    }

    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(device, &supported_features);


    return indices.is_complete() &&
           bExtensionSupported &&
           bSwapchainComplete &&
           // TODO: other feature that we required
           supported_features.samplerAnisotropy; // bool
}

bool VulkanState::is_validation_layers_supported()
{
    uint32_t count;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());

    for (const char *layer : m_ValidationLayers)
    {
        bool ok = false;
        for (const auto &layer_properties : layers)
        {
            if (0 != std::strcmp(layer, layer_properties.layerName)) {
                return false;
            }
        }
    }
    return true;
}

bool VulkanState::is_device_extension_support(VkPhysicalDevice device)
{
    uint32_t count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available_extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available_extensions.data());

    // Global predefine extensions that we need
    std::set<std::string> required_extensions(m_DeviceExtensions.begin(), m_DeviceExtensions.end());

    for (const auto &extension : available_extensions)
    {
        required_extensions.erase(extension.extensionName);
    }
    return required_extensions.empty();
}
