#pragma once

#include "core/base.h"

#include <string>
using namespace std::literals;

#include <cstdio>
#include <fstream>


#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

#include <vector>

struct GLFWState;


struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR        capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   present_modes;

    VkSurfaceFormatKHR ChooseSwapSurfaceFormat();
    VkPresentModeKHR   ChooseSwapPresentMode();
    VkExtent2D         ChooseSwapExtent(GLFWState *m_GLFWState);
};



struct VulkanState
{
    const std::vector<const char *> m_ValidationLayers = {
        "VK_LAYER_KHRONOS_validation",
    };

    const std::vector<const char *> m_DeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, // "VK_KHR_swapchain"
    };
    const bool m_EnableValidationLayers = false;

  private:

    GLFWState   *m_GLFWState = nullptr;
    VkInstance   m_Instance;
    VkSurfaceKHR m_Surface;

    VkDebugUtilsMessengerEXT m_DebugMessengerCallback;
    VkDebugReportCallbackEXT m_DebugReportCallback; // deprecated

    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice         m_LogicalDevice  = VK_NULL_HANDLE;

    VkQueue m_PresentQueue  = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;

    VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;

    VkFormat   m_SwapChainImageFormat;
    VkExtent2D m_SwapChainExtent;

    std::vector<VkImage>       m_SwapChainImages;       // VKImage: Texture
    std::vector<VkImageView>   m_SwapChainImageViews;   //
    std::vector<VkFramebuffer> m_SwapChainFrameBuffers; // VkFramebuffer:����renderpass ��Ŀ��ͼƬ����������VkImage����



    VkPipeline   m_graphicsPipeLine; // drawing ʱ GPU ��Ҫ��״̬��Ϣ���Ͻṹ�壬������shader,��դ��,depth��
    VkRenderPass m_renderPass;       // ������Ⱦ������drawing�����������, attachment,subpass


    VkDescriptorPool      m_descriptorPool;
    VkDescriptorSet       m_DescriptorSet;
    VkDescriptorSetLayout m_descriptorSetLayout; // ���������������һ��������
    VkPipelineLayout      m_pipelineLayout;      // ���߲��� linera �� Optimal(tileƽ��)��



    // std::vector<Vertex>   m_vertices;
    std::vector<uint32_t> m_indices;

    VkBuffer       m_vertexBuffer;
    VkDeviceMemory m_vertexBufferMemory;

    VkBuffer       m_indexBuffer;
    VkDeviceMemory m_indexBufferMemory;

    VkBuffer       m_uniformBuffer;
    VkDeviceMemory m_uniformBUfferMemory;

    VkImage        m_textureImage;
    VkDeviceMemory m_textureImageMemory;
    VkImageView    m_textureImageView; // ���� ����ͼ��
    VkSampler      m_textureSampler;   // texture����

    VkImage        m_depthImage;
    VkDeviceMemory m_depthImageMemory;
    VkImageView    m_depthImageView;

    VkCommandPool                m_commandPool;    // ����أ����滺�������ڴ� ����ֱ�ӵ��ú������л��ƻ��ڴ�����ȣ� ����д���������������
    std::vector<VkCommandBuffer> m_commandBuffers; // �������

    VkSemaphore m_imageAvailableSemaphore; // Э���������¼����ź����������� դ��(?Fench) һ������դ��״̬�� ��Ҫ���ڶ����ڻ���������ͬ������
    VkSemaphore m_renderFinishedSemaphore; // դ����Ҫ���� ���ó��� ��������Ⱦ����ͬ��


    struct QueueFamilyIndices
    {
        int graphics_family  = -1;
        int supported_family = -1;

        bool is_complete()
        {
            return graphics_family >= 0 && supported_family >= 0;
        }

        static QueueFamilyIndices Query(VkSurfaceKHR surface, VkPhysicalDevice device, VkQueueFlagBits flags = VK_QUEUE_GRAPHICS_BIT)
        {
            QueueFamilyIndices indices;
            uint32_t           count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
            std::vector<VkQueueFamilyProperties> queue_families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, queue_families.data());


            int family_index = 0;
            for (const auto &queue_family : queue_families) {
                if (queue_family.queueCount > 0)
                {
                    if (queue_family.queueFlags & flags) {

                        indices.graphics_family = family_index;
                    }
                    VkBool32 bSupport = false;
                    vkGetPhysicalDeviceSurfaceSupportKHR(device, family_index, surface, &bSupport);
                    if (bSupport)
                    {
                        indices.supported_family = family_index;
                    }
                }

                if (indices.is_complete()) {
                    break;
                }
                ++family_index;
            }

            return indices;
        }
    };


  public:

    void Init(GLFWState *glfw_state)
    {
        m_GLFWState = glfw_state;

        create_instance();

        if (m_EnableValidationLayers)
        {
            setup_debug_messenger_ext();
            setup_report_callback_ext();
        }

        create_surface();
        find_and_pick_physical_device();

        create_logic_device();

        create_swapchain();
        create_iamge_views();
        create_renderpass();            // specify how many attachments(color,depth,etc)
        create_descriptor_set_layout(); // specify how many binding (UBO,uniform,etc)
        createGraphicsPipeline();

        createCommandPool();

        createDepthResources();

        createFramebuffers();

        createTextureImage();
        createTextureImageView();
        createTextureSampler();

        loadModel();
        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffer();

        createDescriptorPool();
        createDescriptorSet();

        createCommandBuffers();
        createSemaphores();
    }



    void OnUpdate()
    {
        updateUniformBuffer();
        drawFrame();
    }

    void OnPostUpdate()
    {
        vkDeviceWaitIdle(m_LogicalDevice);
    };

    void Uninit()
    {
        vkDeviceWaitIdle(m_LogicalDevice);

        cleanupSwapChain();

        vkDestroySampler(m_LogicalDevice, m_textureSampler, nullptr);

        vkDestroyImageView(m_LogicalDevice, m_textureImageView, nullptr);

        vkDestroyImage(m_LogicalDevice, m_textureImage, nullptr);
        vkFreeMemory(m_LogicalDevice, m_textureImageMemory, nullptr);

        vkDestroyDescriptorPool(m_LogicalDevice, m_descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_LogicalDevice, m_descriptorSetLayout, nullptr);

        vkDestroyBuffer(m_LogicalDevice, m_uniformBuffer, nullptr);
        vkFreeMemory(m_LogicalDevice, m_uniformBUfferMemory, nullptr);

        vkDestroyBuffer(m_LogicalDevice, m_indexBuffer, nullptr);
        vkFreeMemory(m_LogicalDevice, m_indexBufferMemory, nullptr);

        vkDestroyBuffer(m_LogicalDevice, m_vertexBuffer, nullptr);
        vkFreeMemory(m_LogicalDevice, m_vertexBufferMemory, nullptr);

        vkDestroySemaphore(m_LogicalDevice, m_renderFinishedSemaphore, nullptr);
        vkDestroySemaphore(m_LogicalDevice, m_imageAvailableSemaphore, nullptr);

        vkDestroyCommandPool(m_LogicalDevice, m_commandPool, nullptr);

        vkDestroyDevice(m_LogicalDevice, nullptr);

        if (m_EnableValidationLayers)
        {
            destroy_debug_callback_ext();
            destroy_debug_report_callback_ext();
        }

        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        vkDestroyInstance(m_Instance, nullptr);
    }

  private:

    void create_instance();
    void create_surface();
    void create_logic_device();
    void create_swapchain();
    void init_swapchain_images();
    void create_iamge_views();
    void create_renderpass();
    void create_descriptor_set_layout();
    void createGraphicsPipeline();
    void createCommandPool();
    void createDepthResources();
    void createFramebuffers();
    void createTextureImage();
    void createTextureImageView();
    void createTextureSampler();
    void loadModel();
    void createVertexBuffer();
    void createIndexBuffer();
    void createUniformBuffer();
    void createDescriptorPool();

    void createDescriptorSet();

    void createCommandBuffers();

    void createSemaphores();

    void recreateSwapChain();

  private:


    void cleanupSwapChain()
    {
        vkDestroyImageView(m_LogicalDevice, m_depthImageView, nullptr);
        vkDestroyImage(m_LogicalDevice, m_depthImage, nullptr);
        vkFreeMemory(m_LogicalDevice, m_depthImageMemory, nullptr);

        for (size_t i = 0; i < m_SwapChainFrameBuffers.size(); ++i)
        {
            vkDestroyFramebuffer(m_LogicalDevice, m_SwapChainFrameBuffers[i], nullptr);
        }

        vkDestroyPipeline(m_LogicalDevice, m_graphicsPipeLine, nullptr);
        vkDestroyPipelineLayout(m_LogicalDevice, m_pipelineLayout, nullptr);
        vkDestroyRenderPass(m_LogicalDevice, m_renderPass, nullptr);

        vkDestroyImageView(m_LogicalDevice, m_textureImageView, nullptr);

        for (size_t i = 0; i < m_SwapChainImageViews.size(); ++i)
        {
            vkDestroyImageView(m_LogicalDevice, m_SwapChainImageViews[i], nullptr);
        }

        vkDestroySwapchainKHR(m_LogicalDevice, m_SwapChain, nullptr);
    }

  private: // mainLoop �׶κ���

    void drawFrame()
    {

        // 1. �����ź��� : creteSemphores() in initVulkan()
        // 2. �ӽ�������ȡͼ��
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(m_LogicalDevice, m_SwapChain, std::numeric_limits<uint64_t>::max(), m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
        /*
        vkAcquireNextImageKHR����ǰ��������������ϣ����ȡ��ͼ����߼��豸�ͽ�������
        ����������ָ����ȡ��Чͼ��Ĳ���timeout����λ���롣����ʹ��64λ�޷������ֵ��ֹtimeout��
        ����������������ָ��ʹ�õ�ͬ�����󣬵�presentation���������ͼ��ĳ��ֺ��ʹ�øö������źš�
        ����ǿ�ʼ���Ƶ�ʱ��㡣������ָ��һ���ź���semaphore����դ���������ߡ�����Ŀ���ԣ����ǻ�ʹ��imageAvailableSemaphore��
        ���Ĳ���ָ���������г�Ϊavailable״̬��ͼ���Ӧ���������������������ý�����ͼ������swapChainImages��ͼ��VkImage������ʹ���������ѡ����ȷ�����������
        */

        vkQueueWaitIdle(m_PresentQueue);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) { // swap chain �� surface ���ټ��ݣ����ɽ�����Ⱦ
            std::cout << "Swap chain no compatible with surface! Adjusting... " << std::endl;
            recreateSwapChain();
            return;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) { // SUPOPTIMAL �������Կ�����surface�ύͼ�񣬵���surface����Ϥ����ƥ��׼ȷ������ƽ̨�������µ���ͼ��ĳߴ���Ӧ��С
            panic("failed to acquire swap chain image");
        }



        // 3. �ύ�������
        VkSubmitInfo         submitInfo         = {};
        VkSemaphore          waitSemaphores[]   = {m_imageAvailableSemaphore};
        VkPipelineStageFlags waitStages[]       = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore          signalSemaphores[] = {m_renderFinishedSemaphore};
        {
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores    = waitSemaphores;
            submitInfo.pWaitDstStageMask  = waitStages;

            submitInfo.commandBufferCount = 1; // Bug fixed: Lost 2 define
            submitInfo.pCommandBuffers    = &m_commandBuffers[imageIndex];

            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores    = signalSemaphores;
        }

        if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            panic("failed to submit draw command buffer!");
        }

        // 4. ������ύ�� swapchain ������, ��ʾ����Ļ��
        VkPresentInfoKHR presentInfo  = {};
        VkSwapchainKHR   swapChains[] = {m_SwapChain};
        {
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores    = signalSemaphores; // ָ����Ҫ�ȴ����ź����� �� VkSubmitInfo һ��

            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains    = swapChains; // ָ���ύ�� target swapchain �� ÿ�� swapchain ������
            presentInfo.pImageIndices  = &imageIndex;

            presentInfo.pResults = nullptr; // ָ��У����ֵ�����ǿ���ֱ��ʹ�� vkQueuePresentKHR() �ķ���ֵ�ж�  Optional
        }

        result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            std::cout << "warn of present queue" << std::endl;
            recreateSwapChain();
        }
        else if (result != VK_SUCCESS) {
            panic("failed to present image/imageIndex to swapchain!");
        }
    }

    void updateUniformBuffer()
    {
        // using clock = std::chrono::high_resolution_clock;

        // static auto startTime   = clock::now(); // static ��ʼʱ��
        // auto        currentTime = clock::now();

        // // ����ʱ����������ת�ı���
        // float time = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count() / 1000.0f;

        // UniformBufferObject ubo = {};
        // {
        //     //  ��һ��mat�� ��ת�Ƕȡ� ��ת��
        //     ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

        //     // �ӽǷ��򣨽Ƕȣ�������λ�á�ͷ��ָ����
        //     ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

        //     // Fov 45�Ƚǣ� ���߱ȣ� ��/Զ�ü���
        //     ubo.proj = glm::perspective(glm::radians(45.0f), m_swapChainExtent.width / (float)m_swapChainExtent.height, 0.1f, 10.0f);

        //     // GLM Ϊ openGL ��Ƶģ� ���Ĳü�����(proj.y)��vulkan�෴
        //     ubo.proj[1][1] *= -1;
        // }

        // // �� ubo ���󿽱��������õ� uniformBufferMemory ��
        // void *data;
        // vkMapMemory(m_logicalDevice, m_uniformBUfferMemory, 0, sizeof(ubo), 0, &data);
        // memcpy(data, &ubo, sizeof(ubo));
        // vkUnmapMemory(m_logicalDevice, m_uniformBUfferMemory);
    }

  private:

    const VkDebugUtilsMessengerCreateInfoEXT &get_debug_messenger_create_info_ext()
    {
        static VkDebugUtilsMessengerCreateInfoEXT info{
            .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = // nullptr,
            [](VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
               VkDebugUtilsMessageTypeFlagsEXT             type,
               const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
               void                                       *pUserData) -> VkBool32 {
                std::cerr << std::format("[ Validation Layer ] severity: {}, type: {} --> {}",
                                         std::to_string(severity),
                                         type,
                                         pCallbackData->pMessage)
                          << std::endl;
                return VK_FALSE;
            },
        };

        return info;
    }

    void setup_debug_messenger_ext()
    {
        NE_ASSERT(m_EnableValidationLayers, "Validation layers requested, but not available!");

        const VkDebugUtilsMessengerCreateInfoEXT &create_info = get_debug_messenger_create_info_ext();

        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
        if (!func) {
            panic("failed to find debugger crate function! {}", VK_ERROR_EXTENSION_NOT_PRESENT);
        }
        VkResult ret = func(m_Instance, &create_info, nullptr, &m_DebugMessengerCallback);
        if (VK_SUCCESS != ret) {
            panic("failed to set up debug messenger! {}", ret);
        }
    }

    void setup_report_callback_ext()
    {
        NE_ASSERT(m_EnableValidationLayers, "Validation layers requested, but not available!");

        VkDebugReportCallbackCreateInfoEXT report_cb_create_info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = VK_DEBUG_REPORT_WARNING_BIT_EXT |
                     VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                     VK_DEBUG_REPORT_ERROR_BIT_EXT,
            .pfnCallback = nullptr,
            .pUserData   = nullptr,
        };
        report_cb_create_info.pfnCallback = // static VKAPI_ATTR VkBool32 VKAPI_CALL  debugReportCallback(
            [](VkDebugReportFlagsEXT      flagss,
               VkDebugReportObjectTypeEXT flags_messageType,
               uint64_t                   obj,
               size_t                     location,
               int32_t                    code,
               const char                *layerPrefix,
               const char                *msg,
               void                      *pUserData) {
                std::cerr << "validation layer: " << msg << std::endl;
                return VK_FALSE;
            };


        auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(m_Instance,
                                                                              "vkCreateDebugReportCallbackEXT");
        if (!func) {
            panic("failed to set up debug callback!", VK_ERROR_EXTENSION_NOT_PRESENT);
        }
        VkResult ret = func(m_Instance, &report_cb_create_info, nullptr, &m_DebugReportCallback);
        if (VK_SUCCESS != ret) {
            panic("failed to set up debug callback!", ret);
        }
    }

    void destroy_debug_callback_ext()
    {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
        if (nullptr != func) {
            func(m_Instance, m_DebugMessengerCallback, nullptr);
        }
    }


    void destroy_debug_report_callback_ext()
    {
        auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugReportCallbackEXT");
        if (nullptr != func) {
            func(m_Instance, m_DebugReportCallback, nullptr);
        }
    }

    void find_and_pick_physical_device()
    {
        uint32_t count  = 0;
        VkResult result = vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
        NE_ASSERT(count > 0, "Failed to find GPUs with Vulkan support!");

        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(m_Instance, &count, devices.data());

        NE_TRACE("--Physical Device {}", count);

        for (const auto &device : devices)
        {
            NE_TRACE("  Physic device addr: {}", (void *)device);
            if (is_device_suitable(device))
            {
                m_PhysicalDevice = device;
                break;
            }
        }

        NE_ASSERT(m_PhysicalDevice != VK_NULL_HANDLE, "failed to find a suitable GPU!");
    }

    bool                    is_device_suitable(VkPhysicalDevice device);
    QueueFamilyIndices      query_queue_families(VkPhysicalDevice device, VkQueueFlagBits flags = VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT);
    bool                    is_validation_layers_supported();
    bool                    is_device_extension_support(VkPhysicalDevice device);
    SwapChainSupportDetails query_swapchain_supported(VkPhysicalDevice device);

    static std::vector<char> readFile(const std::string &filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            panic("failed to open file!");
        }

        size_t            fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);

        file.close();

        return buffer;
    }

    VkShaderModule create_shader_module(const std::vector<uint32_t> &spv_binary)
    {
        VkShaderModuleCreateInfo shaderModuleCreateInfo = {
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = spv_binary.size() * 4, // VUID-VkShaderModuleCreateInfo-codeSize-08735
            .pCode    = spv_binary.data(),
        };

        VkShaderModule shaderModule;
        if (VK_SUCCESS != vkCreateShaderModule(m_LogicalDevice, &shaderModuleCreateInfo, nullptr, &shaderModule))
        {
            panic("failed to create shader module");
        }
        return shaderModule;
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

        // Ϊ�������ҵ����ʵ��ڴ�����
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if (typeFilter & (1 << i) &&                                                 // vertexbuffer
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) // ����
            {
                return i;
            }
        }
        panic("failed to find suitable memory type!");
        return -1;
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory)
    {

        // 1. ����������,
        VkBufferCreateInfo bufferInfo = {};
        {
            bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size        = size;
            bufferInfo.usage       = usage;                     // ʹ��bit��ָ�����ʹ��Ŀ��
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // ��ռģʽ�������л�������������������������
        }

        if (vkCreateBuffer(m_LogicalDevice, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
        {
            panic("failed to create vertexbuffer");
        }

        // 2. �ڴ�����VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
        VkMemoryRequirements memoryRequirements;
        vkGetBufferMemoryRequirements(m_LogicalDevice, buffer, &memoryRequirements);

        // 3. �ڴ����
        VkMemoryAllocateInfo allocInfo = {};
        {
            allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize  = memoryRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties);
        }
        if (vkAllocateMemory(m_LogicalDevice, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS)
        {
            panic("failed to allocate vertex buffer memory!");
        }

        // 4. ������������
        vkBindBufferMemory(m_LogicalDevice, buffer, bufferMemory, 0);
    }

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
    {
        // ����һ����ʱ���������,���ύ��������
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        // ���� copy
        VkBufferCopy copyRegion = {};
        {
            copyRegion.srcOffset = 0; // Optional
            copyRegion.dstOffset = 0; // Optional
            copyRegion.size      = size;
        }
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        // �ύ��ɾ����ʱ ���� ����
        endSingleTimeCommands(commandBuffer);
    }

    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usageBits, VkMemoryPropertyFlags properties, VkImage &image, VkDeviceMemory &imageMemory)
    {
        // ���� ��Ա VKImage-textureiamge �� ��Ա textureMemory ����
        VkImageCreateInfo imageInfo = {};
        {
            imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType     = VK_IMAGE_TYPE_2D; // 1D:�����Ҷ�ͼ 2D������ͼ�� 3D����������
            imageInfo.extent.width  = static_cast<uint32_t>(width);
            imageInfo.extent.height = static_cast<uint32_t>(height);
            imageInfo.extent.depth  = 1;
            imageInfo.mipLevels     = 1; //
            imageInfo.arrayLayers   = 1;

            imageInfo.format = format;

            imageInfo.tiling = tiling; // linear/optimal ���صĲ���

            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // undefine/preinitialized gpu������ʹ�ã���һ���仯����/�ᱣ������

            imageInfo.usage = usageBits; // ������������Ŀ��/��shader�ɷ���ͼ���mesh��ɫ

            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.flags   = 0; // Optional
        }

        if (vkCreateImage(m_LogicalDevice, &imageInfo, nullptr, &image) != VK_SUCCESS)
        {
            panic("failed to create image!");
        }

        // ���� texImg ���ڴ�
        VkMemoryRequirements memRequirements; // ��ѯ֧�ֵ��ڴ�����
        vkGetImageMemoryRequirements(m_LogicalDevice, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        {
            allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize  = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
        }
        if (vkAllocateMemory(m_LogicalDevice, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
        {
            panic("failed to allocate image memory!");
        }

        // �� m_texImg �� vuilkan
        vkBindImageMemory(m_LogicalDevice, image, imageMemory, 0);
    }

    VkCommandBuffer beginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo allocInfo = {};
        {
            allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool        = m_commandPool;
            allocInfo.commandBufferCount = 1;
        }

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(m_LogicalDevice, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo = {};
        {
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        }

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void endSingleTimeCommands(VkCommandBuffer &commandBuffer)
    {
        // TODO(may fix): �Ƿ���Ҫ���� commandBuffer

        // ���� �����¼
        vkEndCommandBuffer(commandBuffer);

        // �ύ���� �� ������ �� command pool
        VkSubmitInfo submitInfo       = {};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &commandBuffer;

        vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE); // �ύ�� graphic queue

        vkQueueWaitIdle(m_GraphicsQueue);

        vkFreeCommandBuffers(m_LogicalDevice, m_commandPool, 1, &commandBuffer); // ����� commandbuffer
    }

    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        // �任ͼ��Ĳ���
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        // image memory barrier �����������ڷ�����Դ��ͬʱ����ͬ�������� ���ƻ�����һ����д������ٶ�ȡ
        VkImageMemoryBarrier barrier = {};
        {
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

            barrier.oldLayout = oldLayout; // ����ʹ�� VK_IAMGE_LAYOUT_UNDEFINED ���棬 ����Ѿ��������Ѿ�������ͼ���е�����
            barrier.newLayout = newLayout;

            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; //  ��Դ��������������ʹ��barrier ��Ҫ queue cluster���������������ϵ���� ignored (����Ĭ��ֵ��
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            barrier.image = image;

            if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            {
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                // �Ƿ�֧�� stencil
                if (hasStencilComponent(format)) {
                    barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
                }
            }
            else {
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            }

            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;
        }
        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;
        {
            // �� buffer ���ڴ�
            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                sourceStage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            // ���ڴ� �� �豸
            else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                sourceStage      = VK_PIPELINE_STAGE_TRANSFER_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            // ��imageView �� ���ͼ
            else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                sourceStage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            }
            else {
                panic("unsupported layout transition!");
            }
        }

        vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage,
            destinationStage,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);


        endSingleTimeCommands(commandBuffer);
    }

    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferImageCopy region = {};
        {
            region.bufferOffset      = 0;
            region.bufferRowLength   = 0; // ���ڴ��ж��
            region.bufferImageHeight = 0;

            region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel       = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount     = 1;

            region.imageOffset = {0, 0, 0};
            region.imageExtent = {width, height, 1};
        }

        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        endSingleTimeCommands(commandBuffer);
    }

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
    {

        VkImageViewCreateInfo imageViewCreateInfo = {};
        {
            imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewCreateInfo.image = image;

            // ͼ�������ʽ viewType: 1D textures/2D textures/3D textures/cube maps
            imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCreateInfo.format   = format;

            // ��ɫͨ��ӳ���߼�
            imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            // Specify target of usage of image
            imageViewCreateInfo.subresourceRange.aspectMask     = aspectFlags;
            imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
            imageViewCreateInfo.subresourceRange.levelCount     = 1;
            imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            imageViewCreateInfo.subresourceRange.layerCount     = 1;
        }

        VkImageView imageView;

        if (vkCreateImageView(m_LogicalDevice, &imageViewCreateInfo, nullptr, &imageView) != VK_SUCCESS)
        {
            panic("failed to create image view!");
        }

        return imageView;
    }



    // foarmat �Ƿ�֧�� stencil test
    bool hasStencilComponent(VkFormat format)
    {
        return format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }
};

#include "glm/glm.hpp"


struct Vertex
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;


    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription = {};
        {
            bindingDescription.binding   = 0;
            bindingDescription.stride    = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            /*
            VK_VERTEX_INPUT_RATE_VERTEX: 移动到每个顶点后的下一个数据条目
            VK_VERTEX_INPUT_RATE_INSTANCE : 在每个instance之后移动到下一个数据条目
                我们不会使用instancing渲染，所以坚持使用per - vertex data方式。
            */
        }

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions()
    {
        // 顶点属性
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions = {};
        {
            // 顶点段 2 floats vec2
            attributeDescriptions[0].binding  = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset   = offsetof(Vertex, pos);
            /*
            format参数描述了属性的类型。该格式使用与颜色格式一样的枚举，看起来有点乱。下列的着色器类型和格式是比较常用的搭配。
                float: VK_FORMAT_R32_SFLOAT
                vec2 : VK_FORMAT_R32G32_SFLOAT
                vec3 : VK_FORMAT_R32G32B32_SFLOAT
                vec4 : V_FORMAT_R32G32B32A32_SFLOAT
            颜色类型(SFLOAT, UINT, SINT) 和位宽度应该与着色器输入的类型对应匹配。如下示例：
                ivec2 : VK_FORMAT_R32G32_SINT, 由两个32位有符号整数分量组成的向量
                uvec4 : VK_FORMAT_R32G32B32A32_UINT, 由四个32位无符号正式分量组成的向量
                double : VK_FORMAT_R64_SFLOAT, 双精度浮点数(64 - bit)
            */

            // 颜色段 3 floats vec3
            attributeDescriptions[1].binding  = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset   = offsetof(Vertex, color);

            //  纹理贴图的坐标(四角的一角） floats vec2
            attributeDescriptions[2].binding  = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format   = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset   = offsetof(Vertex, texCoord);
        }

        return attributeDescriptions;
    }
};