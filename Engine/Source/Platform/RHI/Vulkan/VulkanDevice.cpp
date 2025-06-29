#include "VulkanDevice.h"

#include <Core/Base.h>
#include <Core/Log.h>

#include <array>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <set>



#define panic(...) NE_CORE_ASSERT(false, __VA_ARGS__);



void VulkanState::createInstance()
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
        const VkDebugUtilsMessengerCreateInfoEXT &debug_messenger_create_info = getDebugMessengerCreateInfoExt();

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

    // Load debug function pointers after instance creation
    // loadDebugFunctionPointers();
}


// Assign the m_physicalDevice, m_PresentQueue, m_GraphicsQueue
void VulkanState::createLogicDevice()
{
    QueueFamilyIndices family_indices = QueueFamilyIndices::query(m_Surface, m_PhysicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queue_crate_infos;
    std::set<uint32_t>                   unique_queue_families = {
        static_cast<uint32_t>(family_indices.graphicsFamilyIdx),
        static_cast<uint32_t>(family_indices.supportedFamilyIdx)};

    float queue_priority = 1.0f;
    for (uint32_t queue_family : unique_queue_families) {
        queue_crate_infos.push_back({
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .queueFamilyIndex = queue_family,
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

    vkGetDeviceQueue(m_LogicalDevice, family_indices.supportedFamilyIdx, 0, &m_PresentQueue);
    vkGetDeviceQueue(m_LogicalDevice, family_indices.graphicsFamilyIdx, 0, &m_GraphicsQueue);
    NE_ASSERT(m_PresentQueue != VK_NULL_HANDLE, "failed to get present queue!");
    NE_ASSERT(m_GraphicsQueue != VK_NULL_HANDLE, "failed to get graphics queue!");
}

void VulkanState::createCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = QueueFamilyIndices::query(m_Surface, m_PhysicalDevice);

    VkCommandPoolCreateInfo poolInfo{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = static_cast<uint32_t>(queueFamilyIndices.graphicsFamilyIdx),
    };

    if (vkCreateCommandPool(m_LogicalDevice, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to create command pool!");
    }
}

void VulkanState::createCommandBuffers()
{
    m_commandBuffers.resize(m_swapChain.getImages().size());

    VkCommandBufferAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = m_commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size()),
    };

    if (vkAllocateCommandBuffers(m_LogicalDevice, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to allocate command buffers!");
    }
}

void VulkanState::createSemaphores()
{
    VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    if (vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &m_imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &m_renderFinishedSemaphore) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to create semaphores!");
    }
}

void VulkanState::setupDebugMessengerExt()
{
    if (!m_EnableValidationLayers) {
        return;
    }


    VkDebugUtilsMessengerCreateInfoEXT createInfo = getDebugMessengerCreateInfoExt();

    if (pfnCreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessengerCallback) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to set up debug messenger!");
    }
    NE_CORE_INFO("Debug messenger setup successfully");
}

void VulkanState::setupReportCallbackExt()
{

    if (!m_EnableValidationLayers) {
        return;
    }
    if (!pfnCreateDebugReportCallbackEXT) {
        NE_CORE_WARN("pfnCreateDebugReportCallbackEXT is not loaded, skipping debug report callback setup");
        return;
    }

    VkDebugReportCallbackCreateInfoEXT createInfo{
        .sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
        .flags       = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
        .pfnCallback = [](VkDebugReportFlagsEXT      flags,
                          VkDebugReportObjectTypeEXT objectType,
                          uint64_t                   object,
                          size_t                     location,
                          int32_t                    messageCode,
                          const char                *pLayerPrefix,
                          const char                *pMessage,
                          void                      *pUserData) -> VkBool32 {
            NE_CORE_ERROR("[Validation Layer] {}: {}", pLayerPrefix, pMessage);
            return VK_FALSE;
        },
    };

    if (pfnCreateDebugReportCallbackEXT(m_Instance, &createInfo, nullptr, &m_DebugReportCallback) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to set up debug report callback!");
    }

    NE_CORE_INFO("Debug report callback setup successfully");
}

void VulkanState::destroyDebugCallBackExt()
{
    if (m_EnableValidationLayers) {
        pfnDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessengerCallback, nullptr);
    }
}

void VulkanState::destroyDebugReportCallbackExt()
{
    if (m_EnableValidationLayers) {
        pfnDestroyDebugReportCallbackEXT(m_Instance, m_DebugReportCallback, nullptr);
        m_DebugReportCallback = VK_NULL_HANDLE;
    }
}


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
        VulkanSwapChainSupportDetails swapchain_support_details = VulkanSwapChainSupportDetails::query(device, m_Surface);

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
        NE_CORE_DEBUG("Checking validation layer: {}", layer);
        bool ok = false;
        for (const auto &layer_properties : layers)
        {
            if (0 == std::strcmp(layer, layer_properties.layerName)) {
                return true;
            }
        }
    }
    return false;
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



void VulkanState::createDepthResources()
{

    VkFormat depthFormat = VulkanUtils::findSupportedImageFormat(
        m_PhysicalDevice,
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);


    auto ext = m_swapChain.getExtent();

    VulkanUtils::createImage(
        m_LogicalDevice,
        m_PhysicalDevice,
        ext.width,
        ext.height,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_depthImage,
        m_depthImageMemory);


    m_depthImageView = VulkanUtils::createImageView(m_LogicalDevice,
                                                    m_depthImage,
                                                    depthFormat,
                                                    VK_IMAGE_ASPECT_DEPTH_BIT);

    VulkanUtils::transitionImageLayout(m_LogicalDevice,
                                       m_commandPool,
                                       m_GraphicsQueue,
                                       m_depthImage,
                                       depthFormat,
                                       VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void VulkanState::loadDebugFunctionPointers()
{
    if (m_EnableValidationLayers) {
        // Load debug utils messenger functions
        pfnCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
        pfnDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");

        // Load debug report callback functions (deprecated but still used)
        pfnCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)
            vkGetInstanceProcAddr(m_Instance, "vkCreateDebugReportCallbackEXT");
        pfnDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)
            vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugReportCallbackEXT");

        if (!pfnCreateDebugUtilsMessengerEXT) {
            NE_CORE_WARN("pfnCreateDebugUtilsMessengerEXT is not loaded, skipping debug utils messenger setup");
            pfnCreateDebugUtilsMessengerEXT = nullptr; // Set to nullptr to avoid using it later
        }
        if (!pfnCreateDebugReportCallbackEXT) {
            NE_CORE_WARN("pfnCreateDebugReportCallbackEXT is not loaded, skipping debug report callback setup");
            pfnCreateDebugReportCallbackEXT = nullptr; // Set to nullptr to avoid using it later
        }
        if (!pfnDestroyDebugUtilsMessengerEXT) {
            NE_CORE_WARN("pfnDestroyDebugUtilsMessengerEXT is not loaded, skipping debug utils messenger cleanup");
            pfnDestroyDebugUtilsMessengerEXT = nullptr; // Set to nullptr to avoid using it later
        }
        if (!pfnDestroyDebugReportCallbackEXT) {
            NE_CORE_WARN("pfnDestroyDebugReportCallbackEXT is not loaded, skipping debug report callback cleanup");
            pfnDestroyDebugReportCallbackEXT = nullptr; // Set to nullptr to avoid using it later
        }
    }
}

void VulkanState::createVertexBuffer()
{
    // Define triangle vertices (centered triangle)
    m_triangleVertices = {
        {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}, // Bottom vertex (red)
        {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}, // Top left vertex (blue)
        {{0.5f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},  // Top right vertex (green)
    };

    VkDeviceSize bufferSize = sizeof(m_triangleVertices[0]) * m_triangleVertices.size();

    // Create staging buffer
    VkBuffer       stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    VulkanUtils::createBuffer(
        m_LogicalDevice,
        m_PhysicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    // Copy vertex data to staging buffer
    void *data;
    vkMapMemory(m_LogicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_triangleVertices.data(), (size_t)bufferSize);
    vkUnmapMemory(m_LogicalDevice, stagingBufferMemory);

    // Create vertex buffer
    VulkanUtils::createBuffer(
        m_LogicalDevice,
        m_PhysicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vertexBuffer,
        m_vertexBufferMemory);

    // Copy from staging buffer to vertex buffer
    VulkanUtils::copyBuffer(m_LogicalDevice,
                            m_commandPool,
                            m_GraphicsQueue,
                            stagingBuffer,
                            m_vertexBuffer,
                            bufferSize);

    // Cleanup staging buffer
    vkDestroyBuffer(m_LogicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(m_LogicalDevice, stagingBufferMemory, nullptr);
}

void VulkanState::createUniformBuffer()
{
    VkDeviceSize bufferSize = sizeof(CameraData);

    VulkanUtils::createBuffer(
        m_LogicalDevice,
        m_PhysicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_uniformBuffer,
        m_uniformBufferMemory);

    // Map the uniform buffer memory so we can update it
    vkMapMemory(m_LogicalDevice, m_uniformBufferMemory, 0, bufferSize, 0, &m_uniformBufferMapped);
}

void VulkanState::updateUniformBuffer()
{
    CameraData ubo{};
    ubo.viewProjection = glm::mat4(1.0f); // Identity matrix for now (no transformation)

    memcpy(m_uniformBufferMapped, &ubo, sizeof(ubo));
}

void VulkanState::drawTriangle()
{
    // Wait for previous frame
    vkWaitForFences(m_LogicalDevice, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_LogicalDevice, 1, &m_inFlightFence);

    // Acquire next image
    uint32_t imageIndex;
    VkResult result = m_swapChain.acquireNextImage(imageIndex, m_imageAvailableSemaphore);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        NE_CORE_ASSERT(false, "Failed to acquire swap chain image!");
    }

    // Update uniform buffer
    updateUniformBuffer();

    // Reset and begin command buffer
    VkCommandBuffer commandBuffer = m_commandBuffers[imageIndex];
    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "Failed to begin recording command buffer!");
    }

    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass        = m_renderPass.getRenderPass();
    renderPassInfo.framebuffer       = m_renderPass.getFramebuffers()[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapChain.getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color        = {{1.0f, 0.0f, 1.0f, 1.0f}}; // Clear color: black
    clearValues[1].depthStencil = {1.0f, 0};                  // Clear depth to 1.0

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues    = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind graphics pipeline
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.getPipeline());

#if 1 // need to set the vulkan dynamic state, see VkPipelineDynamicStateCreateInfo
    // Set viewport
    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(m_swapChain.getExtent().width);
    viewport.height   = static_cast<float>(m_swapChain.getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);


    // Set scissor
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapChain.getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
#endif

    // Bind vertex buffer
    VkBuffer     vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[]       = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    // Draw triangle
    vkCmdDraw(commandBuffer, static_cast<uint32_t>(m_triangleVertices.size()), 1, 0, 0);


#if 1 // multiple renderpass
    // vkCmdNextSubpass2()
    // vkCmdNextSubpass()
#endif

    // End render pass and command buffer
    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "Failed to record command buffer!");
    }

    // Submit command buffer
    VkSemaphore          waitSemaphores[]   = {m_imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[]       = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore          signalSemaphores[] = {m_renderFinishedSemaphore};


    VkSubmitInfo submitInfo{
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = waitSemaphores,
        .pWaitDstStageMask    = waitStages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = signalSemaphores,
    };


    if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_inFlightFence) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "Failed to submit draw command buffer!");
    }

    // Present
    result = m_swapChain.presentImage(imageIndex, m_renderFinishedSemaphore, m_PresentQueue);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapChain();
    }
    else if (result != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "Failed to present swap chain image!");
    }
}

void VulkanState::recreateSwapChain()
{
    m_swapChain.recreate();
    // For now, just wait for device to be idle
    // In a full implementation, this would recreate the swap chain when the window is resized
    vkDeviceWaitIdle(m_LogicalDevice);
    NE_CORE_WARN("Swap chain recreation requested - not fully implemented yet");
}

void VulkanState::createFences()
{
    VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT // Start in signaled state
    };

    if (vkCreateFence(m_LogicalDevice, &fenceInfo, nullptr, &m_inFlightFence) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to create fence!");
    }
}
