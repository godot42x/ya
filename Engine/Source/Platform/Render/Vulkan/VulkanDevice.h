#pragma once

#include "Core/Delegate.h"
#include "Core/Log.h"


#include "Render/Device.h"
#include "SDL3/SDL_video.h"
#include "VulkanRenderPass.h"
#include "VulkanUtils.h"


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
#include <vulkan/vulkan.h>

#include <vector>

#include "WindowProvider.h"

#include <Render/Shader.h>

struct GLFWState;

#define panic(...) NE_CORE_ASSERT(false, __VA_ARGS__);


struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR        capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   present_modes;

    // first valid surface format
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat();
    // first valid present mode
    VkPresentModeKHR ChooseSwapPresentMode();
    VkExtent2D       ChooseSwapExtent(WindowProvider *provider);

    static SwapChainSupportDetails query(VkPhysicalDevice device, VkSurfaceKHR surface);
};



struct VulkanState
{
    friend struct VulkanUtils;

    const std::vector<const char *> m_ValidationLayers = {
        "VK_LAYER_KHRONOS_validation",
    };

    const std::vector<const char *> m_DeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, // "VK_KHR_swapchain"
    };
    const bool m_EnableValidationLayers = false;

  private:

    VkInstance   m_Instance;
    VkSurfaceKHR m_Surface;

    VkDebugUtilsMessengerEXT m_DebugMessengerCallback;
    VkDebugReportCallbackEXT m_DebugReportCallback; // deprecated

    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice         m_LogicalDevice  = VK_NULL_HANDLE;

    VkQueue m_PresentQueue  = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;

    VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;

    VkFormat                 m_SwapChainImageFormat;
    VkColorSpaceKHR          m_SwapChainColorSpace;
    VkExtent2D               m_SwapChainExtent;
    std::vector<VkImage>     m_SwapChainImages; // VKImage: Texture
    std::vector<VkImageView> m_SwapChainImageViews;

    VulkanRenderPass m_renderPass;

    VkPipeline m_graphicsPipeLine;


    VkDescriptorPool      m_descriptorPool;
    VkDescriptorSet       m_DescriptorSet;
    VkDescriptorSetLayout m_descriptorSetLayout;
    VkPipelineLayout      m_pipelineLayout;



    // std::vector<Vertex>   m_vertices;
    // std::vector<uint32_t> m_indices;

    // VkBuffer       m_vertexBuffer;
    // VkDeviceMemory m_vertexBufferMemory;

    // VkBuffer       m_indexBuffer;
    // VkDeviceMemory m_indexBufferMemory;

    // VkBuffer       m_uniformBuffer;
    // VkDeviceMemory m_uniformBUfferMemory;


    VkSampler m_defaultTextureSampler;


    // how to get the max size of texture slot in current physical device?
    // AI: The maximum number of texture slots is determined by the physical device's capabilities,
    // specifically the `maxPerStageDescriptorSamplers` property of the `VkPhysicalDeviceLimits` structure.
    int m_maxTextureSlots = -1; // TODO: query the physical device for this value

    struct Texture
    {};
    struct Texture2D : public Texture
    {};
    struct VulkanTexture2D : public Texture2D
    {
        VkImage     image;
        VkImageView imageView;
    };

    std::vector<std::shared_ptr<VulkanTexture2D>> m_textures; // for multi-texture

    std::vector<VkImage>     m_textureImages;     // for multi-texture
    std::vector<VkImageView> m_textureImageViews; // for multi-texture


    VkImage        m_depthImage;
    VkDeviceMemory m_depthImageMemory;
    VkImageView    m_depthImageView;

    VkCommandPool                m_commandPool;    // ����أ����滺�������ڴ� ����ֱ�ӵ��ú������л��ƻ��ڴ�����ȣ� ����д���������������
    std::vector<VkCommandBuffer> m_commandBuffers; // �������

    VkSemaphore m_imageAvailableSemaphore;
    VkSemaphore m_renderFinishedSemaphore;

    VulkanUtils helper; // helper for vulkan utils

    struct QueueFamilyIndices
    {
        int graphics_family  = -1;
        int supported_family = -1;

        bool is_complete()
        {
            return graphics_family >= 0 && supported_family >= 0;
        }

        static QueueFamilyIndices query(VkSurfaceKHR surface, VkPhysicalDevice device, VkQueueFlagBits flags = VK_QUEUE_GRAPHICS_BIT)
        {
            QueueFamilyIndices indices;
            uint32_t           count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
            std::vector<VkQueueFamilyProperties> queue_families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, queue_families.data());


            int familyIndex = 0;
            for (const auto &queue_family : queue_families) {
                if (queue_family.queueCount > 0)
                {
                    if (queue_family.queueFlags & flags) {

                        indices.graphics_family = familyIndex;
                    }
                    VkBool32 bSupport = false;
                    vkGetPhysicalDeviceSurfaceSupportKHR(device, familyIndex, surface, &bSupport);
                    if (bSupport)
                    {
                        indices.supported_family = familyIndex;
                    }
                }

                if (indices.is_complete()) {
                    break;
                }
                ++familyIndex;
            }

            return indices;
        }
    };


  public:

    WindowProvider *_windowProvider = nullptr;
    void           *nativeWindow    = nullptr;
    template <typename T>
    void *getNativeWindow()
    {
        return static_cast<T *>(nativeWindow);
    }

    void init(WindowProvider *windowProvider)
    {
        _windowProvider = windowProvider;
        nativeWindow    = _windowProvider->getNativeWindowPtr();
        create_instance();

        if (m_EnableValidationLayers)
        {
            setup_debug_messenger_ext();
            setup_report_callback_ext();
        }
        bool ok = onCreateSurface.ExecuteIfBound(&(*m_Instance), &m_Surface);
        NE_CORE_ASSERT(ok, "Failed to create surface!");

        searchPhysicalDevice();
        createLogicDevice(); // logical device, present queue, graphics queue
        createCommandPool();

        createSwapchain();
        helper.onRecreateSwapchain(this); // TODO: use recreateSwapChain() here

        initSwapchainImages();
        create_iamge_views();

        // Initialize and create render pass
        m_renderPass.initialize(m_LogicalDevice, m_PhysicalDevice, m_SwapChainImageFormat);
        m_renderPass.createRenderPass();

        create_descriptor_set_layout(); // specify how many binding (UBO,uniform,etc)
        createDepthResources();

        // Create framebuffers using the render pass
        m_renderPass.createFramebuffers(m_SwapChainImageViews, m_depthImageView, m_SwapChainExtent);

        createTextureSampler();

        // loadModel();
        // createIndexBuffer();
        // createUniformBuffer();

        createDescriptorPool();
        createDescriptorSet();

        createCommandBuffers();
        createSemaphores();
    }



    void OnUpdate()
    {
        modifedStaticData();
        updateUniformBuffer();
        drawFrame();
        submitFrame();
    }

    void OnPostUpdate()
    {
        vkDeviceWaitIdle(m_LogicalDevice);
    };

    void Uninit()
    {
        vkDeviceWaitIdle(m_LogicalDevice);

        cleanupSwapChain();

        vkDestroySampler(m_LogicalDevice, m_defaultTextureSampler, nullptr);


        vkDestroyDescriptorPool(m_LogicalDevice, m_descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_LogicalDevice, m_descriptorSetLayout, nullptr);


        vkDestroySemaphore(m_LogicalDevice, m_renderFinishedSemaphore, nullptr);
        vkDestroySemaphore(m_LogicalDevice, m_imageAvailableSemaphore, nullptr);

        vkDestroyCommandPool(m_LogicalDevice, m_commandPool, nullptr);

        vkDestroyDevice(m_LogicalDevice, nullptr);


        if (m_EnableValidationLayers)
        {
            destroy_debug_callback_ext();
            destroy_debug_report_callback_ext();
        }

        onReleaseSurface.ExecuteIfBound(&(*m_Instance), &m_Surface);
        // vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        vkDestroyInstance(m_Instance, nullptr);
    }

    void createGraphicsPipeline(std::unordered_map<EShaderStage::T, std::vector<uint32_t>> spv_binaries);

  public:
    Delegate<bool(VkInstance, VkSurfaceKHR *inSurface)> onCreateSurface;
    Delegate<void(VkInstance, VkSurfaceKHR *inSurface)> onReleaseSurface;
    Delegate<std::vector<const char *>()>               onGetRequiredExtensions;



  private:

    void create_instance();


    void createLogicDevice();
    void createSwapchain();
    void initSwapchainImages();
    void create_iamge_views();
    void create_descriptor_set_layout();
    void createCommandPool();
    void createDepthResources();

    void createTextureSampler();

    // void loadModel();
    void createVertexBuffer(void *data, std::size_t size, VkBuffer &outVertexBuffer, VkDeviceMemory &outVertexBufferMemory);
    void createIndexBuffer();
    // void createUniformBuffer(uint32_t size);
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

        // Clean up render pass and framebuffers
        m_renderPass.cleanup();

        vkDestroyPipeline(m_LogicalDevice, m_graphicsPipeLine, nullptr);
        vkDestroyPipelineLayout(m_LogicalDevice, m_pipelineLayout, nullptr);

        // vkDestroyImageView(m_LogicalDevice, m_textureImageView, nullptr);

        for (size_t i = 0; i < m_SwapChainImageViews.size(); ++i)
        {
            vkDestroyImageView(m_LogicalDevice, m_SwapChainImageViews[i], nullptr);
        }

        vkDestroySwapchainKHR(m_LogicalDevice, m_SwapChain, nullptr);
    }

  private: // mainLoop �׶κ���

    void drawFrame()
    {
        for (size_t i = 0; i < m_commandBuffers.size(); i++)
        {
            // 2. ����������¼
            VkCommandBufferBeginInfo beginInfo = {};
            {
                beginInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags            = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
                beginInfo.pInheritanceInfo = nullptr;
                // Optional
                /*
                    VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT: use the command buffer only once, after which it will be reset.
                    VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT :
                    VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT :
                */
            }

            /*<-------------------------------------------------------------------------------->*/
            vkBeginCommandBuffer(m_commandBuffers[i], &beginInfo); // S ���������¼

            std::array<VkClearValue, 2> clearValues = {
                VkClearValue{
                    .color = {0.0f, 0.0f, 0.0f, 1.0f}, // clear color
                },
                VkClearValue{
                    .depthStencil = {1.0f, 0} // clear depth
                },
            };

            VkRenderPassBeginInfo renderPassInfo = {};
            {
                renderPassInfo.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassInfo.renderPass  = m_renderPass.getRenderPass();
                renderPassInfo.framebuffer = m_renderPass.getFramebuffers()[i];

                renderPassInfo.renderArea.offset = {0, 0};
                renderPassInfo.renderArea.extent = m_SwapChainExtent;

                renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
                renderPassInfo.pClearValues    = clearValues.data();
            }

            vkCmdBeginRenderPass(m_commandBuffers[i],
                                 &renderPassInfo,
                                 VK_SUBPASS_CONTENTS_INLINE);
            // VK_SUBPASS_CONTENTS_INLINE : the commands in the command buffer will be executed inline within the render pass.
            // VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : the command buffer will contain secondary command buffers that will be executed within the render pass.

            vkCmdBindDescriptorSets(m_commandBuffers[i],
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout,
                                    0,
                                    1,
                                    &m_DescriptorSet,
                                    0,
                                    nullptr);

            vkCmdBindPipeline(m_commandBuffers[i],
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_graphicsPipeLine);
            // VkBuffer     vertexBuffers[] = {vertexBuffer};
            // VkDeviceSize offsets[]       = {0};

            // // why drawing here?
            // vkCmdBindVertexBuffers(m_commandBuffers[i], 0, 1, &vertexBuffer, offsets);
            // vkCmdBindIndexBuffer(m_commandBuffers[i], indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            // the DrawCall, can be mutiple in a pass?
            // vkCmdDrawIndexed(m_commandBuffers[i], static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);

            vkCmdEndRenderPass(m_commandBuffers[i]);

            auto result = (vkEndCommandBuffer(m_commandBuffers[i]));
            NE_CORE_ASSERT(result == VK_SUCCESS, "failed to record command buffer!");
        }
    }
    void submitFrame()
    {

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(m_LogicalDevice,
                                                m_SwapChain,
                                                std::numeric_limits<uint64_t>::max(),
                                                m_imageAvailableSemaphore,
                                                VK_NULL_HANDLE,
                                                &imageIndex);

        vkQueueWaitIdle(m_PresentQueue);

        // Q: why VK_ERROR_OUT_OF_DATE_KHR?
        // AI: If the swap chain is no longer compatible with the surface (e.g., window resized), we need to recreate it.
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            std::cout << "Swap chain no compatible with surface! Adjusting... " << std::endl;
            recreateSwapChain();
            return;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            // VK_SUBOPTIMAL_KHR means the swapchain is still valid but not optimal
            panic("failed to acquire swap chain image");
        }



        VkSemaphore          waitSemaphores[] = {m_imageAvailableSemaphore};
        VkPipelineStageFlags waitStages[]     = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

        VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphore};

        VkSubmitInfo submitInfo = {};
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
            NE_CORE_ASSERT(false, "failed to submit draw command buffer!");
        }

        //   Present the image to the swapchain
        VkResult presentResult = VK_SUCCESS;

        VkPresentInfoKHR presentInfo  = {};
        VkSwapchainKHR   swapChains[] = {m_SwapChain};
        {
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores    = signalSemaphores;

            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains    = swapChains;
            presentInfo.pImageIndices  = &imageIndex;

            presentInfo.pResults = &presentResult; // Optional, can be used to get results of present operation
        }

        result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            std::cout << "warn of present queue" << std::endl;
            recreateSwapChain();
        }
        else if (result != VK_SUCCESS) {
            NE_CORE_ASSERT(false, "failed to present image/imageIndex to swapchain!");
        }
    }

    void modifedStaticData()
    {
        // change vertex data, index data
        // like the model's position, rotation, scale, etc.
        // update the vertex buffer, index buffer, uniform buffer, etc.
        // if needed
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
            NE_CORE_ASSERT(false, "failed to find debugger crate function! {}", (int)VK_ERROR_EXTENSION_NOT_PRESENT);
        }
        VkResult ret = func(m_Instance, &create_info, nullptr, &m_DebugMessengerCallback);
        if (VK_SUCCESS != ret) {
            NE_CORE_ASSERT(false, "failed to set up debug messenger! {}", (int)ret);
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
            panic("failed to set up debug callback!", (int)VK_ERROR_EXTENSION_NOT_PRESENT);
        }
        VkResult ret = func(m_Instance, &report_cb_create_info, nullptr, &m_DebugReportCallback);
        if (VK_SUCCESS != ret) {
            panic("failed to set up debug callback!", (int)ret);
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

    void searchPhysicalDevice()
    {
        uint32_t count  = 0;
        VkResult result = vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
        NE_CORE_ASSERT(count > 0, "Failed to find GPUs with Vulkan support!");

        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(m_Instance, &count, devices.data());

        NE_CORE_TRACE("Physical Device count: {}", count);

        for (const auto &device : devices)
        {
            NE_CORE_TRACE("  Physic device addr: {}", (void *)device);
            if (is_device_suitable(device))
            {
                m_PhysicalDevice = device;
                break;
            }
        }

        NE_ASSERT(m_PhysicalDevice != VK_NULL_HANDLE, "failed to find a suitable GPU!");
    }
    bool is_device_suitable(VkPhysicalDevice device);
    bool is_validation_layers_supported();
    bool is_device_extension_support(VkPhysicalDevice device);

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


struct VulkanDevice : public LogicalDevice
{
    VulkanState     _vulkanState;
    WindowProvider *windowProvider = nullptr;

    virtual bool init(const InitParams &params) override
    {
        windowProvider = &params.windowProvider;

#if USE_SDL

        auto sdlWindow = static_cast<SDLWindowProvider *>(windowProvider);
        _vulkanState.onCreateSurface.set([sdlWindow](VkInstance instance, VkSurfaceKHR *surface) {
            return sdlWindow->createVkSurface(instance, surface);
        });
        _vulkanState.onReleaseSurface.set([sdlWindow](VkInstance instance, VkSurfaceKHR *surface) {
            sdlWindow->destroyVkSurface(instance, surface);
        });
        _vulkanState.onGetRequiredExtensions.set([sdlWindow]() {
            return sdlWindow->getVkInstanceExtensions();
        });

#endif

        _vulkanState.init(windowProvider);

        // UNIMPLEMENTED();
        return false;
    }

    void destroy() override
    {
        _vulkanState.Uninit();
        windowProvider = nullptr;
    }
};


#undef panic