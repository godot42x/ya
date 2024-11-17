

#include "vulkan_state.h"

#include "core/base.h"

#include <set>
#include <unordered_map>

#include "renderer/shader/shader.h"
#include "window/glfw_state.h"

#include "vertex.h"


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

VkExtent2D SwapChainSupportDetails::ChooseSwapExtent(GLFWState *m_GLFWState)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width, height;
    m_GLFWState->GetWindowSize(width, height);
    VkExtent2D actual_extent = {(uint32_t)width, (uint32_t)height};

    actual_extent.width = std::max(
        capabilities.minImageExtent.width,
        std::min(capabilities.maxImageExtent.width, actual_extent.width));
    actual_extent.height = std::max(
        capabilities.minImageExtent.height,
        std::min(capabilities.maxImageExtent.height, actual_extent.height));

    return actual_extent;
}

void VulkanState::create_instance()
{
    if (m_EnableValidationLayers && !is_validation_layers_supported()) {
        panic("validation layers requested, but not available!");
    }

    VkApplicationInfo app_info{
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = "Hello Triangle",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "No Engine",
        .apiVersion         = VK_API_VERSION_1_0,
    };

    std::vector<const char *> extensions = m_GLFWState->GetVKRequiredExtensions();
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
        panic("failed to create instance!");
    }
}

void VulkanState::create_surface()
{
    if constexpr (1) // use GLFW
    {
        if (VK_SUCCESS != glfwCreateWindowSurface(m_Instance,
                                                  m_GLFWState->m_Window,
                                                  nullptr,
                                                  &m_Surface))
        {
            panic("failed to create window surface");
        }
    }
    else {
        panic("Unknow window surface creation provider");
    }
}

// Assign the m_physicalDevice, m_PresentQueue, m_GraphicsQueue
void VulkanState::create_logic_device()
{
    QueueFamilyIndices family_indices = QueueFamilyIndices::Query(m_Surface, m_PhysicalDevice);

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
    SwapChainSupportDetails swapchain_support_details = query_swapchain_supported(m_PhysicalDevice);

    VkSurfaceFormatKHR surface_format = swapchain_support_details.ChooseSwapSurfaceFormat();
    VkPresentModeKHR   presentMode    = swapchain_support_details.ChooseSwapPresentMode();

    m_SwapChainExtent      = swapchain_support_details.ChooseSwapExtent(m_GLFWState);
    m_SwapChainImageFormat = surface_format.format;


    // Image count
    uint32_t image_count = swapchain_support_details.capabilities.minImageCount + 1;
    if (swapchain_support_details.capabilities.maxImageCount > 0 &&
        image_count > swapchain_support_details.capabilities.maxImageCount)
    {
        image_count = swapchain_support_details.capabilities.maxImageCount;
    }

    // Create info
    VkSwapchainCreateInfoKHR swapchain_create_info{
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = m_Surface,
        .minImageCount    = image_count,
        .imageFormat      = m_SwapChainImageFormat,
        .imageColorSpace  = surface_format.colorSpace,
        .imageExtent      = m_SwapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform     = swapchain_support_details.capabilities.currentTransform, // No transform op
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = presentMode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = VK_NULL_HANDLE,
    };


    // TODO: only need to query once? no high performance required
    QueueFamilyIndices indices = query_queue_families(m_PhysicalDevice);

    uint32_t queue_family_indices[] = {
        (uint32_t)indices.graphics_family,
        (uint32_t)indices.supported_family,
    };

    // TODO: what's the difference between concurrent and exclusive sharing
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
    if (ret != VK_SUCCESS) {
        panic("failed to create swapchain!");
    }
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
        m_SwapChainImageViews[i] = createImageView(m_SwapChainImages[i],
                                                   m_SwapChainImageFormat,
                                                   VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void VulkanState::create_renderpass()
{
    // TODO: Different and make all hardcodes as parameters

    // color
    VkAttachmentDescription color_attachment_desc = {
        .format         = m_SwapChainImageFormat,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED, // Bug fix: init-undef sub-attachOptimal final-presentSrcKhr
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference color_attachment_Ref = {
        .attachment = 0, // index
        .layout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    };


    // depth
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    {
        auto candidate_formats = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT,
        };
        VkImageTiling        tiling        = VK_IMAGE_TILING_OPTIMAL;
        VkFormatFeatureFlags feature_flags = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        for (VkFormat candidate_format : candidate_formats)
        {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, candidate_format, &properties);

            switch (tiling) {

            case VK_IMAGE_TILING_OPTIMAL:
            {
                if ((properties.optimalTilingFeatures & feature_flags) == feature_flags) {
                    depth_format = candidate_format;
                }
            }
            case VK_IMAGE_TILING_LINEAR:
            {
                if ((properties.linearTilingFeatures & feature_flags) == feature_flags) {
                    depth_format = candidate_format;
                }
            }
            case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT:
            case VK_IMAGE_TILING_MAX_ENUM:
                NE_ASSERT(false, "tiling is not supported!");
            }
        }
    }
    NE_ASSERT(depth_format != VK_FORMAT_UNDEFINED, "failed to find supported format!");

    VkAttachmentDescription depth_attachment_desc = {
        .format         = depth_format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference depth_attachment_ref = {
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };


    VkSubpassDescription subpass_desc = {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,                     // Maybe add more for mouse picking
        .pColorAttachments    = &color_attachment_Ref, //  where the frag output vec4 color will store
        /*
            pInputAttachments
            pResolveAttachments
            pDepthStencilAttachment
            pPreserveAttachments
        */
        .pDepthStencilAttachment = &depth_attachment_ref,
    };

    VkSubpassDependency subpass_dependency = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, //  swapchain output
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    std::array<VkAttachmentDescription, 2> attachments = {
        color_attachment_desc,
        depth_attachment_desc,
    };

    // RenderPass creatInfo ��Ⱦͨ��
    VkRenderPassCreateInfo renderpass_create_info = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(attachments.size()),
        .pAttachments    = attachments.data(),
        .subpassCount    = 1,
        .pSubpasses      = &subpass_desc,
        .dependencyCount = 1,
        .pDependencies   = &subpass_dependency,
    };

    if (VK_SUCCESS != vkCreateRenderPass(m_LogicalDevice, &renderpass_create_info, nullptr, &m_renderPass))
    {
        panic("failed to crete render pass!");
    }
    NE_ASSERT(m_renderPass, "failed to crete render pass!");
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

    if (VK_SUCCESS != vkCreateDescriptorSetLayout(m_LogicalDevice, &layout_crate_info, nullptr, &m_descriptorSetLayout)) {
        panic("failed to create descriptor set layout");
    }
    NE_ASSERT(m_descriptorSetLayout, "failed to create descriptor set layout");
}

void VulkanState::createGraphicsPipeline()
{
    /************************** Shader Stages ***********************************************/

    /* shader ģ�� ������ͼ�ι��߿ɱ�̽׶εĹ���    */


    GLSLScriptProcessor                                     processor("engine/shaders/default.glsl");
    std::unordered_map<EShaderStage, std::vector<uint32_t>> spv_binaries;
    bool                                                    Ok = processor.TakeSpv(spv_binaries);
    NE_ASSERT(Ok, "failed to take spv binaries");

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

    VertexInput v_input(0);
    v_input.AddAttribute(VK_FORMAT_R32G32B32_SFLOAT, "postion")
        .AddAttribute(VK_FORMAT_R32G32B32_SFLOAT, "color")
        .AddAttribute(VK_FORMAT_R32G32_SFLOAT, "texture_coord");

    // the description of vertex(declaration) should compatible with the  shader module
    auto bindingDescription    = v_input.GetBindingDescription();
    auto attributeDescriptions = v_input.GetAttributeDescriptions();

    // vertex input State createInfo
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &bindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions    = attributeDescriptions.data(),
    };
    /************************** Shader Stages ***********************************************/


    /****************************** Fix-function State *****************************************/


    // Assembly state cretInfo ( Triangle format in this example)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    {
        inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // VkPipelineInputAssemblyStateCreateInfo�ṹ��������������:����������ʲô���͵ļ���ͼԪ���˽��л��Ƽ��Ƿ����ö��������¿�ʼͼԪ��ͼԪ�����˽ṹ����topologyö��ֵ����:
        // VK_PRIMITIVE_TOPOLOGY_POINT_LIST: ���㵽��
        // VK_PRIMITIVE_TOPOLOGY_LINE_LIST : ������ߣ����㲻����
        // VK_PRIMITIVE_TOPOLOGY_LINE_STRIP : ������ߣ�ÿ���߶εĽ���������Ϊ��һ���߶εĿ�ʼ����
        // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : ������棬���㲻����
        // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : ÿ������ѵ�ĵڶ��������������㶼��Ϊ��һ�������ε�ǰ��������
    }

    // Viewport ���� ͼ��ת����ӳ�䣩�� framebuffer�����ڣ� �Ķ�Ӧ���� �������xy������hw�������minmax
    VkViewport viewport = {};
    {
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = (float)m_SwapChainExtent.width;
        viewport.height   = (float)m_SwapChainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
    }

    // Scissor �ü� ������Щ�����ڵ����ر�����
    VkRect2D scissor = {};
    {
        scissor.offset = {0, 0};
        scissor.extent = m_SwapChainExtent; // ���ӵ㱣��һ��
    }

    // Viewport State cretInfo ����ʹ�òü����ӵ�
    VkPipelineViewportStateCreateInfo viewportState = {};
    {
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports    = &viewport;
        viewportState.scissorCount  = 1;
        viewportState.pScissors     = &scissor;
    }


    // Resterization State cretInfo ��դ�����������ԡ�tansmit ͼ�� fragmentShader ��ɫ��ִ����Ȳ���depth testing����ü��Ͳü����ԣ������Ƿ���� ����ͼԪ���� ���� �߿򣨿�����Ⱦ��
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    {
        rasterizer.sType            = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;

        // ����Զ���ü����ͼԪ���������������������������Ӱ��ͼ��������ã���GPU support; ������ʿ����ͼԪ�����framebuffer
        rasterizer.rasterizerDiscardEnable = VK_FALSE;

        // polygonMode�������β���ͼƬ�����ݡ�������Чģʽ:
        // VK_POLYGON_MODE_FILL: ������������
        // VK_POLYGON_MODE_LINE : ����α�Ե�߿����
        // VK_POLYGON_MODE_POINT : ����ζ�����Ϊ������
        // ʹ���κ�ģʽ�����Ҫ����GPU���ܡ�
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;

        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode  = VK_CULL_MODE_BACK_BIT; // culling/font faces/call back facess/all ��ü��ķ�ʽ
        // rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // ˳ʱ��/��ʱ�� �������˳��
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; /// ��ת y ��������ʱ�����

        // ���ֵ config �����Ϊfalse
        rasterizer.depthBiasEnable         = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // Optional
        rasterizer.depthBiasClamp          = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor    = 0.0f; // Optional
    }

    // Multisample State cretInfo ���ز��� ���ﲻʹ��
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
            // finalcolor = newcolor & colorwritemask
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
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; // blend mthod
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
    }

    // color blend cretInfo
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

    /* ���߲��ֶ����� uniform(Ҳ����DynamicState) �� push values �Ĳ��֣� �� shader ÿһ�� drawing ��ʱ�����*/

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
        panic("filed to create pipeline layout!");
    }
    /************************************* Pipeline Layout ********************************************/


    /************************************* Render Pass ********************************************/
    /*   ������createRenderPass()������          */
    /************************************* Render Pass ********************************************/


    // Pipeline cretInfo
    VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
    {
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

        // p_state ��Ϊ�� state �� create info
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

        pipelineCreateInfo.layout = m_pipelineLayout;

        pipelineCreateInfo.renderPass = m_renderPass; //  ���� renderPass
        pipelineCreateInfo.subpass    = 0;            // subpass ��ͨ�� ������

        /*
        ʵ���ϻ�����������:basePipelineHandle �� basePipelineIndex��Vulkan������ͨ���Ѿ����ڵĹ��ߴ����µ�ͼ�ι��ߡ�
        �����������¹��ߵ��뷨���ڣ���Ҫ�����Ĺ��������йܵ�������ͬʱ����ýϵ͵Ŀ�����ͬʱҲ���Ը������ɹ����л�������������ͬһ�������ߡ�
        ����ͨ��basePipelineHandleָ�����й��ߵľ����Ҳ����������basePipelineIndex���Դ�������һ�����ߡ�
        Ŀǰֻ��һ�����ߣ���������ֻ��Ҫָ��һ���վ����һ����Ч��������
        ֻ����VkGraphicsPipelineCreateInfo��flags�ֶ���Ҳָ����VK_PIPELINE_CREATE_DERIVATIVE_BIT��־ʱ������Ҫʹ����Щֵ��
        */
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE; //  Optional
        pipelineCreateInfo.basePipelineIndex  = -1;             //  Optional

        pipelineCreateInfo.pDepthStencilState = &depthStencil;
    }

    // 1. ���� ���� Pipeline ���浽��Ա����, ���Դ��ݶ�� pipelineCreateInfo ��������� ����
    // 2. �ڶ�������cache ���ڴ洢�͸�����ͨ����ε���VkCreateGraphicsPipelines ������ص����ݣ� �����ڳ���ִ��ʱ���浽һ���ļ��У� �������Լ��ٺ����Ĺ��ߴ����߼�, ������ʱ������
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
    /*
    ��������������ύ��һ���豸���У����Ǽ��������� graphics �� presentaion ���У� ����ִ��
    ��ÿ�� commandPool ֻ�ܷ����ڵ�һ�Ķ����ϣ����������Ҫ���������һ�£�
    �������� ���� ѡ�� ͼ�ζ��д�
    */
    QueueFamilyIndices queueFamilyIndices = query_queue_families(m_PhysicalDevice);

    VkCommandPoolCreateInfo poolInfo = {};
    {
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphics_family;
        poolInfo.flags            = 0; // Optional
                                       /*
                                       ��������־λ����command pools :
                                           VK_COMMAND_POOL_CREATE_TRANSIENT_BIT: ��ʾ��������ǳ�Ƶ�������¼�¼������(���ܻ�ı��ڴ������Ϊ)
                                           VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT : ������������������¼�¼��û�������־�����е��������������һ������
                                       ���ǽ����ڳ���ʼ��ʱ���¼���������������ѭ����main loop�ж��ִ�У�������ǲ���ʹ����Щ��־��
                                       */
    }

    if (vkCreateCommandPool(m_LogicalDevice, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        panic("failed to create commandPool!");
    }
}

void VulkanState::createDepthResources()
{
    /// <summary>
    /// ����������ͼ�����ǲ�һ����Ҫ�ض��ĸ�ʽ����Ϊ���ǲ���ֱ�Ӵӳ����з������ء�
    /// ��������Ҫһ��������׼ȷ�ԣ�����24λ��ʵ�ʳ������ǳ����ġ��м��ַ���Ҫ��ĸ�ʽ��
    ///	  VK_FORMAT_D32_SFLOAT : 32 - bit float depth
    ///	  VK_FORMAT_D32_SFLOAT_S8_UNIT : 32 - bit signed float depth �� 8 - bit stencil component
    ///	  VK_FORMAT_D32_UNORM_S8_UINT : 24 - bit float depth �� 8 - bit stencil component
    /// stencil component ģ���������ģ�����(stencil tests)�����ǿ�������Ȳ�����ϵĸ��Ӳ��ԡ����ǽ���δ�����½���չ����
    /// ���ǿ��Լ�Ϊ VK_FORMAT_D32_SFLOAT ��ʽ����Ϊ����֧���Ƿǳ������ģ����Ǿ����ܵ�����һЩ����������Ҳ�Ǻܺõġ�
    /// ��������һ������ findSupportedFormat �Ӻ�ѡ��ʽ�б��� ��������ֵ�Ľ���ԭ�򣬼���һ���õ�֧�ֵĸ�ʽ��
    /// </summary>

    // 1. �ҵ�֧�ֵ�format
    VkFormat depthFormat = findDepthFormat();

    // 2. ���� m_depthImage ��� �� �ڴ�ռ�
    createImage(m_SwapChainExtent.width, m_SwapChainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImage, m_depthImageMemory);

    // 3. ���� ���ͼ��Ӧ�� view
    m_depthImageView = createImageView(m_depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    // 4. �л�
    transitionImageLayout(m_depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void VulkanState::createFramebuffers()
{
    m_SwapChainFrameBuffers.resize(m_SwapChainImageViews.size());

    for (size_t i = 0; i < m_SwapChainImageViews.size(); ++i)
    {
        std::array<VkImageView, 2> attachments = {
            m_SwapChainImageViews[i],
            m_depthImageView};

        VkFramebufferCreateInfo framebufferInfo = {};
        {
            framebufferInfo.sType      = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_renderPass;

            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments    = attachments.data(); // �󶨵���Ӧ�ĸ��������� VkImageView ����

            framebufferInfo.width  = m_SwapChainExtent.width;
            framebufferInfo.height = m_SwapChainExtent.height;
            framebufferInfo.layers = 1; // ָ��ͼ�����еĲ���
        }

        if (vkCreateFramebuffer(m_LogicalDevice, &framebufferInfo, nullptr, &m_SwapChainFrameBuffers[i]) != VK_SUCCESS)
        {
            panic("failed to create freamebuffer!");
        }
    }
}

void VulkanState::createTextureImage()
{
    // // TODO(enchance): ��������ܹ�Ϊһ���࣬�����������ĳ�Ա����ֻ�ܴ���һ��ͼƬ

    // // ����ͼƬ�� map���������ص���ʱ�������� �������
    // int      texWidth, texHeight, texChannels;
    // stbi_uc *pixels = stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    // if (!pixels)
    // {
    //     panic("failed to load texture image!");
    // }

    // VkDeviceSize imageSize = texWidth * texHeight * 4;

    // VkBuffer       stagingBuffer;
    // VkDeviceMemory stagingBufferMemory;

    // createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    // void *data;
    // vkMapMemory(m_logicalDevice, stagingBufferMemory, 0, imageSize, 0, &data);
    // memcpy(data, pixels, static_cast<size_t>(imageSize));
    // vkUnmapMemory(m_logicalDevice, stagingBufferMemory);

    // stbi_image_free(pixels);


    // // ���� ����Ҫm_textureImage�ϵ� map ӳ��
    // createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_textureImage, m_textureImageMemory);

    // // �仯��ͼͼ�� layout �� VK_IAMGE_LAYOUT_TRANSFER_DST_OPTIMAL
    // // TODO (may fix): old layout ����Ϊ PREINITIALIZED ������δ���� �ò����Ŀ�����
    // transitionImageLayout(m_textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // // ִ�� ������ �� ͼ��� ��������
    // copyBufferToImage(stagingBuffer, m_textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));

    // // �����ı任��׼�� ��ɫ������, �ڻ�copyBufferToImage����������m_textureImage��
    // transitionImageLayout(m_textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // vkDestroyBuffer(m_logicalDevice, stagingBuffer, nullptr);
    // vkFreeMemory(m_logicalDevice, stagingBufferMemory, nullptr);
}

void VulkanState::createTextureImageView()
{
    m_textureImageView = createImageView(m_textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
}

void VulkanState::createTextureSampler()
{
    // ��������
    VkSamplerCreateInfo samplerInfo = {};
    {
        samplerInfo.sType     = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR; // ���� �Ŵ� �� ��С ���ڲ�ֵ��
        samplerInfo.minFilter = VK_FILTER_LINEAR;

        /// <addressMode>
        /// ����ʹ��addressMode�ֶ�ָ��ÿ������ʹ�õ�Ѱַģʽ����Ч��ֵ�����·���
        /// �������ͼ�����Ѿ�����˵�����ˡ���Ҫע����������������Ϊ U��V �� W ���� X��Y �� Z�����������ռ������Լ����
        ///     VK_SAMPLER_ADDRESS_MODE_REPEAT��������ͼ��ߴ��ʱ�����ѭ����䡣
        /// 	VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT����ѭ��ģʽ���ƣ����ǵ�����ͼ��ߴ��ʱ�������÷�����Ч����
        /// 	VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE��������ͼ��ߴ��ʱ�򣬲��ñ�Ե�������ɫ������䡣
        /// 	VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TOEDGE�����Եģʽ���ƣ�����ʹ���������Ե�෴�ı�Ե������䡣
        /// 	VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER������������ͼ��ĳߴ�ʱ������һ����ɫ��䡣
        /// ������ʹ��ʲô����Ѱַģʽ������Ҫ����Ϊ���ǲ�����ͼ��֮����в�����
        /// ����ѭ��ģʽ���ձ�ʹ�õ�һ��ģʽ����Ϊ����������ʵ��������Ƭ�����ǽ�������Ч����
        /// </addressMode>
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // repeat �� texCoord ����[0,1]ʱ�������ѭ�����ƣ�[0,2] ��Ϊ4����
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        samplerInfo.anisotropyEnable = VK_TRUE; // �����������ι���
        samplerInfo.maxAnisotropy    = 16;      // ������Ϊ1

        samplerInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK; // ����ͼ��Χʱ ���ص���ɫ
        samplerInfo.unnormalizedCoordinates = VK_FALSE;                         // ȷ�����귶Χ����ѹ����, Ϊ texWidth,texHeight, ����[0,1]

        samplerInfo.compareEnable = VK_FALSE; // ����  ����������ֵ�Ƚϣ� ������ֵ���ڹ��˲�������Ҫ���� ��Ӱ����ӳ��� precentage-closer filtering ���ٷֱȽ��ƹ�����
        samplerInfo.compareOp     = VK_COMPARE_OP_ALWAYS;

        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod     = 0.0f;
        samplerInfo.maxLod     = 0.0f;
    }

    if (vkCreateSampler(m_LogicalDevice, &samplerInfo, nullptr, &m_textureSampler) != VK_SUCCESS)
    {
        panic("failed to create texture sampler!");
    }
}

void VulkanState::loadModel()
{
    // tinyobj::attrib_t                attrib;
    // std::vector<tinyobj::shape_t>    shapes; // �������е����Ķ������
    // std::vector<tinyobj::material_t> material;
    // std::string                      warn;
    // std::string                      err;

    // if (!tinyobj::LoadObj(&attrib, &shapes, &material, &warn, &err, MODEL_PATH.c_str()))
    // {
    //     panic(err);
    // }

    // for (const auto &shape : shapes)
    // {
    //     for (const auto &index : shape.mesh.indices)
    //     {
    //         Vertex vertex = {};
    //         vertex.pos    = {
    //             attrib.vertices[3 * index.vertex_index + 0], // �� float ���� *3
    //             attrib.vertices[3 * index.vertex_index + 1],
    //             attrib.vertices[3 * index.vertex_index + 2]};
    //         vertex.texCoord = {
    //             attrib.texcoords[2 * index.texcoord_index + 0],
    //             1.0f - attrib.texcoords[2 * index.texcoord_index + 1]};

    //         vertex.color = {1.0f, 1.0f, 1.0f};


    //         m_vertices.push_back(vertex);
    //         m_indices.push_back(m_indices.size());
    //     }
    // }
}

void VulkanState::createVertexBuffer()
{
    // /*
    // ------>��䶥�㻺����(���붥�����ݣ� ��memmap m_vertBufMemory �� cpu �ô� -> copy �������ݵ����ڴ� -> ȡ�� map��
    // method 1: �������򲻻������������ݵ��������У� ��Ҫӳ��һ���ڴ浽��������Ȼ�󿽱�������ڴ���, ���ڴ�ӳ����һ��������ʧ
    // method 2: ����ڴ�ӳ��󣬵��� vkFlushMappedMemoryRanges, ��ȡ�����ڴ�ʱ������ vkInvalidateMappedMemoryRanges
    // ����ѡ�� ���� 1
    // */

    // VkDeviceSize buffersize = sizeof(m_vertices[0]) * m_vertices.size(); // Ҫ�����Ķ������ݴ�С

    // /*************************��ʱ�������� ʹ����ʱ��buffer,buffermemory����ʱ��commandBuffer����������********/

    // // ����ʹ����ʱ�Ļ�����
    // VkBuffer       stagingBuffer; // �� stagingbuffer ������SBmemory
    // VkDeviceMemory stagingBufferMemory;
    // // ���� buffer ���㻺�塢host visible�� host coherent
    // /*
    // ����ʹ��stagingBuffer������stagingBufferMemory����������ӳ�䡢�����������ݡ��ڱ��½�����ʹ�������µĻ�����usage�������ͣ�
    //     VK_BUFFER_USAGE_TRANSFER_SRC_BIT����������������Դ�ڴ洫�������
    //     VK_BUFFER_USAGE_TRANSFER_DST_BIT����������������Ŀ���ڴ洫�������
    // */
    // createBuffer(buffersize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);



    // // map ��ʱ buufer, �Ѷ������ݿ�����ȥ
    // void *data;
    // vkMapMemory(m_logicalDevice, stagingBufferMemory, 0, buffersize, 0, &data); // map ����ʱ�ڴ�
    // memcpy(data, m_vertices.data(), (size_t)buffersize);
    // vkUnmapMemory(m_logicalDevice, stagingBufferMemory); // ֹͣӳ��

    // // ���� �豸�� �� cpu�ô��ϵ� map
    // createBuffer(buffersize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vertexBuffer, m_vertexBufferMemory);


    // //  ���Ѿ��������map���� stagingBuffer, ������ͬ��map�� device �� m_vertexBuffer ����
    // copyBuffer(stagingBuffer, m_vertexBuffer, buffersize); // encapsulation ��װ copy ����


    // // ������ʹ����� stagingBuffer �� stagingBufferMemory
    // vkDestroyBuffer(m_logicalDevice, stagingBuffer, nullptr);
    // vkFreeMemory(m_logicalDevice, stagingBufferMemory, nullptr);
}

void VulkanState::createIndexBuffer()
{
    // ������vertex buffer
    VkDeviceSize bufferSize = sizeof(m_indices[0]) * m_indices.size();

    VkBuffer       stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void *data;
    vkMapMemory(m_LogicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_indices.data(), (size_t)bufferSize);
    vkUnmapMemory(m_LogicalDevice, stagingBufferMemory);

    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indexBuffer, m_indexBufferMemory);

    copyBuffer(stagingBuffer, m_indexBuffer, bufferSize);


    // clean stage data
    vkDestroyBuffer(m_LogicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(m_LogicalDevice, stagingBufferMemory, nullptr);
}

void VulkanState::createUniformBuffer()
{
    VkDeviceSize buffersize = sizeof(UniformBufferObject);

    createBuffer(buffersize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_uniformBuffer, m_uniformBUfferMemory);
}

void VulkanState::createDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 2> poolSizes = {};
    {
        poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = 1;

        poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = 1;
    }

    VkDescriptorPoolCreateInfo poolInfo = {};
    {
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes    = poolSizes.data();

        poolInfo.maxSets = 1; // �������������
    }

    if (vkCreateDescriptorPool(m_LogicalDevice, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
    {
        panic("failed to create descripor pool!");
    }
}

void VulkanState::createDescriptorSet()
{
    // ���� m_descriptor set �ռ�
    VkDescriptorSetLayout       layouts[] = {m_descriptorSetLayout};
    VkDescriptorSetAllocateInfo allocInfo = {};
    {
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = layouts;
    }
    if (vkAllocateDescriptorSets(m_LogicalDevice, &allocInfo, &m_DescriptorSet)) // ����һ������uniform buufer �������ļ���
    {
        panic("failed to allocate descriptor set!");
    }

    // �����ڲ������� �� ��Ҫ������һ�黺����
    // ���� ubo �� layout ��Ϣ
    VkDescriptorBufferInfo bufferInfo = {};
    {
        bufferInfo.buffer = m_uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range  = sizeof(UniformBufferObject);
    }

    VkDescriptorImageInfo image_info{
        .sampler     = m_textureSampler,
        .imageView   = m_textureImageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    std::array<VkWriteDescriptorSet, 2>
        descriptor_writes = {
            VkWriteDescriptorSet{
                .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet           = m_DescriptorSet,
                .dstBinding       = 0,
                .dstArrayElement  = 0,
                .descriptorCount  = 1,
                .descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pImageInfo       = nullptr,     //  Optional
                .pBufferInfo      = &bufferInfo, // ��set ���ڻ�����������ѡ�� pBufferInfo
                .pTexelBufferView = nullptr,     // Optional
            },
            {
                .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet           = m_DescriptorSet,
                .dstBinding       = 1,
                .dstArrayElement  = 0,
                .descriptorCount  = 1,
                .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo       = &image_info, // ����ѡ�� pImage
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
    m_commandBuffers.resize(m_SwapChainFrameBuffers.size());

    // 1. ������������ڴ�
    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
    {
        commandBufferAllocateInfo.sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocateInfo.commandPool = m_commandPool;
        commandBufferAllocateInfo.level       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        /*
        level����ָ�������������������ӹ�ϵ��
            VK_COMMAND_BUFFER_LEVEL_PRIMARY : �����ύ������ִ�У������ܴ�����������������á�
            VK_COMMAND_BUFFER_LEVEL_SECONDARY : �޷�ֱ���ύ�����ǿ��Դ�������������á�
        ���ǲ���������ʹ�ø������������ܣ����ǿ������񣬶��ڸ������������ĳ��ò������а�����
        */
        commandBufferAllocateInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();
    }
    if (vkAllocateCommandBuffers(m_LogicalDevice, &commandBufferAllocateInfo, m_commandBuffers.data()) != VK_SUCCESS)
    {
        panic("failed to allocate command buffers!");
    }

    for (size_t i = 0; i < m_commandBuffers.size(); i++)
    {
        // 2. ����������¼
        VkCommandBufferBeginInfo beginInfo = {};
        {
            beginInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags            = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            beginInfo.pInheritanceInfo = nullptr; // Optional
                                                  /*
                                                  flags��־λ��������ָ�����ʹ�������������ѡ�Ĳ�����������:
                                                      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT: �����������ִ��һ�κ��������¼�¼��
                                                      VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT : ����һ������������������������һ����Ⱦͨ���С�
                                                      VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT : �������Ҳ���������ύ��ͬʱ��Ҳ�ڵȴ�ִ�С�
                                                  ����ʹ�������һ����־����Ϊ���ǿ����Ѿ�����һ֡��ʱ�����˻�����������һ֡��δ��ɡ�pInheritanceInfo�����븨����������ء���ָ��������������̳е�״̬��
                                                  �����������Ѿ�����¼һ�Σ���ô����vkBeginCommandBuffer����ʽ������������������ӵ��������ǲ����ܵġ�
                                                  */
        }

        /*<-------------------------------------------------------------------------------->*/
        vkBeginCommandBuffer(m_commandBuffers[i], &beginInfo); // S ���������¼

        // 3. ������Ⱦͨ��
        VkRenderPassBeginInfo       renderPassInfo = {};
        std::array<VkClearValue, 2> clearValues    = {}; // �������
        {
            clearValues[0].color        = {0.0f, 0.0f, 0.0f, 1.0f};
            clearValues[1].depthStencil = {1.0f, 0}; // ����Ϊ 1.0f�� ���Ϊ 0
        }
        {
            renderPassInfo.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass  = m_renderPass;
            renderPassInfo.framebuffer = m_SwapChainFrameBuffers[i];

            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = m_SwapChainExtent;

            renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues    = clearValues.data();
        }

        vkCmdBeginRenderPass(m_commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE); // S ������Ⱦͨ��
        /// <vkCmdBeginRenderPass>
        ///	����ÿ�������һ���������Ǽ�¼�����������������ڶ�������ָ�����Ǵ��ݵ���Ⱦͨ���ľ�����Ϣ��
        ///	���Ĳ�����������ṩrender pass��ҪӦ�õĻ��������ʹ��������ֵ����һ�� :
        ///		VK_SUBPASS_CONTENTS_INLINE: ��Ⱦ�������Ƕ��������������У�û�и���������ִ�С�
        ///		VK_SUBPASS_CONTENTS_SECONDARY_COOMAND_BUFFERS : ��Ⱦͨ�������Ӹ����������ִ�С�
        ///	���ǲ���ʹ�ø��������������������ѡ���һ����
        /// </vkCmdBeginRenderPass>

        // 4. ������ͼ����
        /// ��ʵ��shader �� descriptor ��
        /// �붥����������ͬ�� ���������� ����graphicsPipeline Ψһ�ģ������Ҫָ���Ƿ� descriptorSet �󶨵�ͼ�λ���������
        vkCmdBindDescriptorSets(m_commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_DescriptorSet, 0, nullptr);

        /// �󶨹���
        vkCmdBindPipeline(m_commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeLine);
        VkBuffer     vertexBuffers[] = {m_vertexBuffer};
        VkDeviceSize offsets[]       = {0};

        /// �󶨶��㻺����
        vkCmdBindVertexBuffers(m_commandBuffers[i], 0, 1, &m_vertexBuffer, offsets);

        /// ������������
        vkCmdBindIndexBuffer(m_commandBuffers[i], m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);


        /// ʹ�� vkCmdDrawIndexed ��������ö���
        // vkCmdDraw(m_commandBuffers[i], static_cast<uint32_t>(vertices.size()), 1, 0, 0);
        /*
        ʵ�ʵ�vkCmdDraw�����е���������˼��һ�£�������˼򵥣�����Ϊ������ǰָ��������Ⱦ��ص���Ϣ���������µĲ�����Ҫָ���������������:
            vertexCount: ��ʹ����û�ж��㻺����������������Ȼ��3��������Ҫ���ơ�
            instanceCount : ����instanced ��Ⱦ�����û��ʹ������1��
            firstVertex : ��Ϊ���㻺������ƫ����������gl_VertexIndex����Сֵ��
            firstInstance : ��Ϊinstanced ��Ⱦ��ƫ������������gl_InstanceIndex����Сֵ��
        */
        vkCmdDrawIndexed(m_commandBuffers[i], static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);


        // 5. ������Ⱦ
        vkCmdEndRenderPass(m_commandBuffers[i]); // E ������Ⱦ

        auto result = (vkEndCommandBuffer(m_commandBuffers[i])); // E ���������¼
        /*<-------------------------------------------------------------------------------->*/
        if (result != VK_SUCCESS)
        {
            panic("failed to record command buffer!");
        }
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

    create_renderpass();
    createGraphicsPipeline();
    createDepthResources();
    createFramebuffers();
    createCommandBuffers();
}

// A physical device is suitable/supported for ...
bool VulkanState::is_device_suitable(VkPhysicalDevice device)
{
    // VkPhysicalDeviceProperties deviceProperties;
    // vkGetPhysicalDeviceProperties(device, &deviceProperties);

    // VkPhysicalDeviceFeatures devicesFeatures;
    // vkGetPhysicalDeviceFeatures(device, &devicesFeatures);

    // return (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    // 	&& devicesFeatures.geometryShader;

    QueueFamilyIndices indices = QueueFamilyIndices::Query(m_Surface, device, VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT);

    bool bExtensionSupported = is_device_extension_support(device);

    bool bSwapchainComplete = false;
    if (bExtensionSupported)
    {
        SwapChainSupportDetails swapchain_support_details = query_swapchain_supported(device);

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
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    bool bLayerFound = false;
    for (const char *layer : m_ValidationLayers)
    {
        for (const auto &layer_properties : available_layers)
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

SwapChainSupportDetails VulkanState::query_swapchain_supported(VkPhysicalDevice device)
{
    SwapChainSupportDetails details;

    // Capabilities
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &details.capabilities);

    // Formats
    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &format_count, nullptr);
    if (format_count != 0)
    {
        details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &format_count, details.formats.data());
    }

    // PresentModes
    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &present_mode_count, nullptr);
    if (present_mode_count != 0)
    {
        details.present_modes.resize(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &present_mode_count, details.present_modes.data());
    }

    return details;
}
