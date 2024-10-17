

#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <set>


#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "delegate.h"

using namespace std::literals;



#include "log.h"

struct UniformBufferObject
{};
enum class ERenderAPI;

namespace std
{
const char *to_string(VkDebugUtilsMessageSeverityFlagBitsEXT bit);
const char *to_string(ERenderAPI bit);
} // namespace std


#include <source_location>
void panic(const std::string &msg, int code = 1)
{
    NE_ERROR(msg);
    std::exit(code);
}


#define NE_ASSERT(expr, ...)             \
    if (!(expr)) {                       \
        panic(std::format(__VA_ARGS__)); \
    }


static enum class ERenderAPI {
    VULKAN = 0,
    OPENGL = 1,
    D3D12  = 2,
    D3D11  = 3,
    METAL  = 4
} RenderAPI = ERenderAPI::VULKAN;


struct App;


struct GLFWState
{
    GLFWwindow *m_Window = nullptr;
    const bool  bVulkan  = true;

    MulticastDelegate<GLFWwindow * /*window*/, int /*width*/, int /*height */>                              OnWindowResized;
    MulticastDelegate<GLFWwindow * /*window*/, int /*key*/, int /*scancode*/, int /*action*/, int /*mods*/> OnKeyboardInput;

    void Init()
    {
        if (GLFW_TRUE != glfwInit())
        {
            panic("Failed to init glfw");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_Window = glfwCreateWindow(1024, 768, "Neon", nullptr, nullptr);
        if (!m_Window)
        {
            glfwTerminate();
            panic("Failed to create window", 2);
        }

        // Config
        glfwMakeContextCurrent(m_Window);
        glfwSwapInterval(1);

        bind_events();
    }

    void Uninit()
    {
        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }


    void OnUpdate()
    {
        glfwPollEvents();
    }


    void GetWindowSize(int &w, int &h)
    {
        glfwGetWindowSize(m_Window, &w, &h);
    }

    std::vector<const char *> GetVKRequiredExtensions()
    {
        NE_ASSERT(RenderAPI == ERenderAPI::VULKAN, "Unsupported RenderAPI: {}", std::to_string(RenderAPI));
        std::vector<const char *> extensions;

        uint32_t     count = 0;
        const char **glfwExtensions;

        glfwExtensions = glfwGetRequiredInstanceExtensions(&count);
        std::cout << "glfwGetRequiredInstanceExtensions: \n";
        for (unsigned int i = 0; i < count; ++i) {
            std::cout << glfwExtensions[i] << "\n";
            extensions.push_back(glfwExtensions[i]);
        }


        return extensions;
    }

  private:
    void bind_events()
    {
        glfwSetWindowUserPointer(m_Window, this);

        // TODO: better event system
        glfwSetWindowSizeCallback(m_Window, [](GLFWwindow *window, int width, int height) {
            if (width == 0 || height == 0)
                return;
            static_cast<GLFWState *>(glfwGetWindowUserPointer(window))->OnWindowResized.Broadcast(window, width, height);
        });
        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow *window) {
            printf("Window Closed...\n");
        });
        glfwSetKeyCallback(m_Window, [](GLFWwindow *window, int key, int scancode, int action, int mods) {
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            static_cast<GLFWState *>(glfwGetWindowUserPointer(window))->OnKeyboardInput.Broadcast(window, key, scancode, action, mods);
        });
    }
};



namespace std
{
const char *to_string(VkDebugUtilsMessageSeverityFlagBitsEXT bit)
{
    switch (bit) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        return "Verbose";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        return "Info";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        return "Warning";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        return "Error";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
        return "Unknown";
    }
    assert(false);
    return "Unknown";
}

const char *to_string(ERenderAPI bit)
{
    switch (bit) {
    case ERenderAPI::VULKAN:
        return "Vulkan";
    case ERenderAPI::OPENGL:
        return "OpenGL";
    case ERenderAPI::D3D12:
        return "D3D12";
    case ERenderAPI::D3D11:
        return "D3D11";
    case ERenderAPI::METAL:
        return "Metal";
    }
    assert(false);
    return "Unknown";
}
} // namespace std

// #define STB_IMAGE_IMPLEMENTATION
// #include<stb/stb_iamge.h>
// #include<tiny_obj_loader.h>



// const int WIDTH  = 800;
// const int HEIGHT = 600;

// const std::string MODEL_PATH   = "models/monkey.obj";
// const std::string TEXTURE_PATH = "textures/texture.jpg";


VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

    if (nullptr != func) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestoryDebugUtilsMessengerEXT(
    VkInstance instance, VkDebugUtilsMessengerEXT debugerMessenger, const VkAllocationCallbacks *pAllocator)
{
    auto func =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (nullptr != func) {
        func(instance, debugerMessenger, pAllocator);
    }
}


void DestoryDebugReportCallbackEXT(
    VkInstance instance, VkDebugReportCallbackEXT reporterCallback, const VkAllocationCallbacks *pAllocator)
{
    auto func =
        (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
    if (nullptr != func) {
        func(instance, reporterCallback, pAllocator);
    }
}



struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR        capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;
};


// const std::vector<Vertex> m_verticesa = {
//     // ���� �� rgb �� texture �Ľ����� ����Ϊopengl���ƶ���ת��y�ᣬ��ʱ�����룬˳ʱ��������ͼ���ۣ�
//     {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
//     {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
//     {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
//     {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},

//     {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
//     {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
//     {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
//     {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}};

// const std::vector<uint16_t> m_indicesa = {
//     0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};


/***************************************************************************************************/



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

    GLFWState *m_GLFWState = nullptr;
    VkInstance m_instance; // Vulkan ʵ��

    VkDebugUtilsMessengerEXT m_debugMessengerCallback; // ��֤�� dubg messen,ger
    VkDebugReportCallbackEXT m_debugReportCallback;    // depercate

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE; // �����Կ��豸
    VkDevice         m_LogicalDevice;                   // �߼��Կ��豸

    VkQueue m_presentQueue;  // չʾ�˿�
    VkQueue m_graphicsQueue; // ͼ�ζ˿�

    VkSurfaceKHR m_surface;

    VkSwapchainKHR m_swapChain; // ����һϵ��ͼƬ


    std::vector<VkFramebuffer> m_swapChainFrameBuffers; // VkFramebuffer:����renderpass ��Ŀ��ͼƬ����������VkImage����
    std::vector<VkImage>       m_swapChainImages;       // VKImage: һ�����Զ�д��Texture
    VkFormat                   m_swapChainImageFormat;
    VkExtent2D                 m_swapChainExtent;
    std::vector<VkImageView>   m_swapChainImageViews;


    VkPipeline   m_graphicsPipeLine; // drawing ʱ GPU ��Ҫ��״̬��Ϣ���Ͻṹ�壬������shader,��դ��,depth��
    VkRenderPass m_renderPass;       // ������Ⱦ������drawing�����������, attachhment,subpass


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
        int graphicsFamily{-1};
        int presentFamily{-1};

        bool isComplete()
        {
            return graphicsFamily >= 0 && presentFamily >= 0;
        }
    } indices;

  public:

    void Init(GLFWState *glfw_state)
    {
        m_GLFWState = glfw_state;

        create_instance();

        setup_debug_messenger();
        setup_report_callback();

        create_surface();
        pickPhysicalDevice();

        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
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
        createSemphores();
    }



    void PreUpdate()
    {
        updateUniformBuffer();
        drawFrame();

        vkDeviceWaitIdle(m_LogicalDevice);
    }
    void PostUpdate()
    {
    }

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

        //  Swap Chain ���
        if (m_EnableValidationLayers)
        {
            DestoryDebugUtilsMessengerEXT(m_instance, m_debugMessengerCallback, nullptr);
        }
        // DestoryDebugReportCallbackEXT(m_instance, m_debugReportCallback, nullptr);

        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        vkDestroyInstance(m_instance, nullptr);
    }

  private:

    void create_instance()
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
            const VkDebugUtilsMessengerCreateInfoEXT &debug_messenger_create_info = get_debug_messenger_create_info();

            instance_create_info.enabledLayerCount   = static_cast<uint32_t>(m_ValidationLayers.size());
            instance_create_info.ppEnabledLayerNames = m_ValidationLayers.data();
            instance_create_info.pNext               = (VkDebugUtilsMessengerCreateInfoEXT *)&debug_messenger_create_info;
        }
        else {
            instance_create_info.enabledLayerCount = 0;
            instance_create_info.pNext             = nullptr;
        }

        VkResult result = vkCreateInstance(&instance_create_info, nullptr, &m_instance);
        if (result != VK_SUCCESS) {
            panic("failed to create instance!");
        }
    }


    void create_surface()
    {
        if constexpr (1) // use GLFW
        {
            if (VK_SUCCESS != glfwCreateWindowSurface(m_instance,
                                                      m_GLFWState->m_Window,
                                                      nullptr,
                                                      &m_surface))
            {
                panic("failed to create window surface");
            }
        }
        else {
            panic("Unknow window surface creation provider");
        }
    }

    void createLogicalDevice()
    {
        QueueFamilyIndices m_QueueFamilyIndices = findQueueFamilies(m_physicalDevice);
        // ����
        float                                queuePriority;
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        int                                  uniqueQueueFamilies[] = {m_QueueFamilyIndices.graphicsFamily, m_QueueFamilyIndices.presentFamily};
        {
            queuePriority = 1.0f;
            for (int queueFamily : uniqueQueueFamilies) {
                VkDeviceQueueCreateInfo queueCreateInfo = {};
                queueCreateInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueCreateInfo.queueFamilyIndex        = queueFamily;
                queueCreateInfo.queueCount              = 1;
                queueCreateInfo.pQueuePriorities        = &queuePriority;
                queueCreateInfos.push_back(queueCreateInfo);
            }
        }

        VkPhysicalDeviceFeatures deviceFeatures = {};
        {
            deviceFeatures.samplerAnisotropy = VK_TRUE;
        }

        VkDeviceCreateInfo deviceCreateInfo = {};
        {
            deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

            // Queue
            deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
            deviceCreateInfo.pQueueCreateInfos    = queueCreateInfos.data();


            // Features
            deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

            // Layer
            deviceCreateInfo.enabledLayerCount = 0;
            if (m_EnableValidationLayers)
            {
                deviceCreateInfo.enabledLayerCount   = static_cast<uint32_t>(m_ValidationLayers.size());
                deviceCreateInfo.ppEnabledLayerNames = m_ValidationLayers.data();
            }
            else {
                deviceCreateInfo.enabledLayerCount = 0;
            }

            // Extension
            deviceCreateInfo.enabledExtensionCount   = static_cast<uint32_t>(m_DeviceExtensions.size());
            deviceCreateInfo.ppEnabledExtensionNames = m_DeviceExtensions.data();
        }

        if (vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_LogicalDevice) != VK_SUCCESS)
        {
            panic("failed to create logical device!");
        }

        vkGetDeviceQueue(m_LogicalDevice, m_QueueFamilyIndices.presentFamily, 0, &m_presentQueue);
        vkGetDeviceQueue(m_LogicalDevice, m_QueueFamilyIndices.graphicsFamily, 0, &m_graphicsQueue);
    }

    void createSwapChain()
    {
        // Swap chain support details
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_physicalDevice);

        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR   presentMode   = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D         extent        = chooseSwapExtent(swapChainSupport.capabilities);

        m_swapChainImageFormat = surfaceFormat.format;
        m_swapChainExtent      = extent;


        // Image count
        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 &&
            imageCount > swapChainSupport.capabilities.maxImageCount)
        {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        // Create info
        VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
        {
            swapChainCreateInfo.sType   = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            swapChainCreateInfo.surface = m_surface;

            swapChainCreateInfo.minImageCount    = imageCount;
            swapChainCreateInfo.imageFormat      = surfaceFormat.format;
            swapChainCreateInfo.imageColorSpace  = surfaceFormat.colorSpace;
            swapChainCreateInfo.imageExtent      = extent;
            swapChainCreateInfo.imageArrayLayers = 1; // ����3D Ӧ�ã� ͼ�����Ϊ1
            swapChainCreateInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;


            QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

            uint32_t queueFamilyIndices[] = {
                (uint32_t)indices.graphicsFamily,
                (uint32_t)indices.presentFamily};

            // 1. VK_SHARING_MODE_EXCLUSIVE: ͬһʱ��ͼ��ֻ�ܱ�һ�����д�ռ�ã�����������д���Ҫ������Ȩ��Ҫ��ȷָ�������ַ�ʽ�ṩ����õ����ܡ�
            // 2. VK_SHARING_MODE_CONCURRENT : ͼ����Ա�������дط��ʣ�����Ҫ��ȷ����Ȩ������ϵ��
            if (indices.graphicsFamily != indices.presentFamily)
            {
                swapChainCreateInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
                swapChainCreateInfo.queueFamilyIndexCount = 2;
                swapChainCreateInfo.pQueueFamilyIndices   = queueFamilyIndices;
            }
            else {
                swapChainCreateInfo.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
                swapChainCreateInfo.queueFamilyIndexCount = 0;       // Optional
                swapChainCreateInfo.pQueueFamilyIndices   = nullptr; // Optional
            }


            swapChainCreateInfo.preTransform   = swapChainSupport.capabilities.currentTransform; // No transform op
            swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;              // alpha ͨ���Ƿ�����������

            swapChainCreateInfo.presentMode = presentMode;
            swapChainCreateInfo.clipped     = VK_TRUE;


            swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE; // ����old swapchain ��Դ�����þ��

            // Create swapchain
            if (vkCreateSwapchainKHR(m_LogicalDevice, &swapChainCreateInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
                panic("faild to create swap chain!");
            }
        }


        // Init swapChainImages
        vkGetSwapchainImagesKHR(m_LogicalDevice, m_swapChain, &imageCount, nullptr);
        m_swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(m_LogicalDevice, m_swapChain, &imageCount, m_swapChainImages.data());
    }

    void createImageViews()
    {
        m_swapChainImageViews.resize(m_swapChainImages.size());

        for (size_t i = 0; i < m_swapChainImages.size(); ++i)
        {
            m_swapChainImageViews[i] = createImageView(m_swapChainImages[i], m_swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    void createRenderPass()
    {
        // ��ɫͨ������������
        VkAttachmentDescription colorAttachment = {};
        {
            colorAttachment.format  = m_swapChainImageFormat; // �뽻�����е�ͼ���ʽƥ��
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;  // �������ز���

            /*  ������ɫ���������
            loadOp��storeOp��������Ⱦǰ����Ⱦ�������ڶ�Ӧ�����Ĳ�����Ϊ������ loadOp ����������ѡ�
                VK_ATTACHMENT_LOAD_OP_LOAD : �����Ѿ������ڵ�ǰ����������
                VK_ATTACHMENT_LOAD_OP_CLEAR : ��ʼ�׶���һ������������������
                VK_ATTACHMENT_LOAD_OP_DONT_CARE : ���ڵ�����δ���壬��������
            �ڻ����µ�һ֡����֮ǰ������Ҫ������ʹ����������������֡������framebufferΪ��ɫ��ͬʱ���� storeOp ��������ѡ�
                VK_ATTACHMENT_STORE_OP_STORE : ��Ⱦ�����ݻ�洢���ڴ棬����֮����ж�ȡ����
                VK_ATTACHMENT_STORE_OP_DONT_CARE : ֡����������������Ⱦ������Ϻ�����Ϊundefined
                ����Ҫ��������Ⱦһ������������Ļ�ϣ���������ѡ��洢������
            */
            colorAttachment.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;  // �����������ε�ʱ����Ҫ�������һ֡�Ļ���
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // ������Ⱦ������

            /*
            loadOp��storeOpӦ������ɫ��������ݣ�ͬʱstencilLoadOp / stencilStoreOpӦ����ģ�����ݡ����ǵ�Ӧ�ó��򲻻����κ�ģ�滺�����Ĳ�������������loading��storing�޹ؽ�Ҫ��
            */
            colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

            /* ����ģ������
            ������֡��������Vulkan��ͨ����VkImage ��������ĳ�����ظ�ʽ�������������������ڴ��еĲ��ֿ��Ի���ԤҪ��imageͼ����еĲ��������ڴ沼�ֵı仯��
            һЩ���õĲ��� :
            VK_IMAGE_LAYOUT_COLOR_ATTACHMET_OPTIMAL: ͼ����Ϊ��ɫ����
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : ͼ���ڽ������б�����
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : ͼ����ΪĿ�꣬�����ڴ�COPY����*/
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Bug fix: init-undef sub-attachOptimal final-presentSrcKhr
            colorAttachment.finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }
        // ����Ⱦͨ����Ҫ ��ǰ�涨��һ������ attachment ������
        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment            = 0; // attchment ������
        colorAttachmentRef.layout                = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

        // ���ͼ�ĸ���Ҫ��
        VkAttachmentDescription depthAttachment = {};
        {
            depthAttachment.format  = findDepthFormat();
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;      // �������һ֡
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // ������

            depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
        // ������Ϣ
        VkAttachmentReference depthAttachmentRef = {};
        depthAttachmentRef.attachment            = 1;
        depthAttachmentRef.layout                = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;



        // Subpass ��ͨ��
        VkSubpassDescription subpass = {};
        {
            subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;                   // ֻʹ�õ�һ�����ģ�帽��
            subpass.pColorAttachments    = &colorAttachmentRef; // ��fragShader ����ʹ�� layout(locaiton=0)out vec4 outColor ������
            /*���Ա���ͨ�����õĸ�����������:
                pInputAttachments: ��������ɫ���ж�ȡ
                pResolveAttachments : ����������ɫ�����Ķ��ز���
                pDepthStencilAttachment : ����������Ⱥ�ģ������
                pPreserveAttachments : ����������ͨ��ʹ�ã��������ݱ�����*/
            subpass.pDepthStencilAttachment = &depthAttachmentRef;
        }

        //  ����ڹ��ߵ���ʼ�׶� ����Ⱦ �� ����Ⱦͨ�� �ڽ���ת������ʱ û�л�ȡͼ�������, ָ��������ϵ�������������������¼�
        VkSubpassDependency dependency = {};
        {
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL; // ��ʽ��ͨ��,ȡ������ �Ƿ��� srcSubpass/dstSubpass ��ָ��
            dependency.dstSubpass = 0;                   // subpass ����

            dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // �ȴ� swapchain ��ɶ�Ӧͼ��Ķ�ȡ����
            dependency.srcAccessMask = 0;

            dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;                              // �ȴ� swapchain output
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // ��ֹ��д����ֱ��ֱ����Ҫ������ɫʱ
        }

        std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

        // RenderPass creatInfo ��Ⱦͨ��
        VkRenderPassCreateInfo renderPassCreateInfo = {};
        {
            renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

            renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            renderPassCreateInfo.pAttachments    = attachments.data();

            renderPassCreateInfo.subpassCount = 1;
            renderPassCreateInfo.pSubpasses   = &subpass;

            renderPassCreateInfo.dependencyCount = 1;
            renderPassCreateInfo.pDependencies   = &dependency;
        }

        // ���� m_renderPass
        if (vkCreateRenderPass(m_LogicalDevice, &renderPassCreateInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
            panic("failed to crete render pass!");
        }
    }

    void createDescriptorSetLayout()
    {
        // UBO layout
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        {
            uboLayoutBinding.binding         = 0;
            uboLayoutBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; // ָ�� binding Ϊ uniform ����
            uboLayoutBinding.descriptorCount = 1;

            uboLayoutBinding.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT; // ������������ɫ�����Ǹ��׶�ʹ�ã�����ֻ�ڶ���
            uboLayoutBinding.pImmutableSamplers = nullptr;                    // ������ͼ��ɼ��й� Optional
        }

        // sampler layout
        VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        {
            samplerLayoutBinding.binding            = 1;
            samplerLayoutBinding.descriptorCount    = 1;
            samplerLayoutBinding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplerLayoutBinding.pImmutableSamplers = nullptr;
            samplerLayoutBinding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
        }


        // ���� layouts
        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        {
            layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings    = bindings.data();
        }

        if (vkCreateDescriptorSetLayout(m_LogicalDevice, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
        {
            panic("failed to create descriptor set laytout");
        }
    }

    void createGraphicsPipeline()
    {
        /************************** Shader Stages ***********************************************/

        /* shader ģ�� ������ͼ�ι��߿ɱ�̽׶εĹ���    */

        // input binary SPIR-V codes
        auto vertShaderCode = readFile("shaders/vert.spv");
        auto fragShaderCode = readFile("shaders/frag.spv");

        // Compile Module
        auto vertShaderModule = createShaderModule(vertShaderCode);
        auto fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
        {
            vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertShaderStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
            vertShaderStageInfo.module = vertShaderModule;
            vertShaderStageInfo.pName  = "main"; // ���õ�����������ڣ�
        }
        VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
        {
            fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragShaderStageInfo.module = fragShaderModule;
            fragShaderStageInfo.pName  = "main"; // ���õ�����������ڣ�
        }

        // Shader Stage cretInfo
        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};


        // vertx input State cretInfo
        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        // auto                                 bindingDescription    = Vertex::getBindingDescription();
        // auto                                 attributeDescriptions = Vertex::getAttributeDescriptions();
        // {
        //     vertexInputInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        //     vertexInputInfo.vertexBindingDescriptionCount   = 1;
        //     vertexInputInfo.pVertexBindingDescriptions      = &bindingDescription;
        //     vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        //     vertexInputInfo.pVertexAttributeDescriptions    = attributeDescriptions.data();
        // }
        /************************** Shader Stages ***********************************************/


        /****************************** Fix-function State *****************************************/

        /* �ýṹ�嶨��̶����߹��ܣ� ���磺 ����װ�䡢viewport���ü�����դ����blending��������*/


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
            viewport.width    = (float)m_swapChainExtent.width;
            viewport.height   = (float)m_swapChainExtent.height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
        }

        // Scissor �ü� ������Щ�����ڵ����ر�����
        VkRect2D scissor = {};
        {
            scissor.offset = {0, 0};
            scissor.extent = m_swapChainExtent; // ���ӵ㱣��һ��
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

    void createCommandPool()
    {
        /*
        ��������������ύ��һ���豸���У����Ǽ��������� graphics �� presentaion ���У� ����ִ��
        ��ÿ�� commandPool ֻ�ܷ����ڵ�һ�Ķ����ϣ����������Ҫ���������һ�£�
        �������� ���� ѡ�� ͼ�ζ��д�
        */
        QueueFamilyIndices queueFamilyIndices = findQueueFamilies(m_physicalDevice);

        VkCommandPoolCreateInfo poolInfo = {};
        {
            poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
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

    void createDepthResources()
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
        createImage(m_swapChainExtent.width, m_swapChainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImage, m_depthImageMemory);

        // 3. ���� ���ͼ��Ӧ�� view
        m_depthImageView = createImageView(m_depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        // 4. �л�
        transitionImageLayout(m_depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }

    void createFramebuffers()
    {
        m_swapChainFrameBuffers.resize(m_swapChainImageViews.size());

        for (size_t i = 0; i < m_swapChainImageViews.size(); ++i)
        {
            std::array<VkImageView, 2> attachments = {
                m_swapChainImageViews[i],
                m_depthImageView};

            VkFramebufferCreateInfo framebufferInfo = {};
            {
                framebufferInfo.sType      = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferInfo.renderPass = m_renderPass;

                framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
                framebufferInfo.pAttachments    = attachments.data(); // �󶨵���Ӧ�ĸ��������� VkImageView ����

                framebufferInfo.width  = m_swapChainExtent.width;
                framebufferInfo.height = m_swapChainExtent.height;
                framebufferInfo.layers = 1; // ָ��ͼ�����еĲ���
            }

            if (vkCreateFramebuffer(m_LogicalDevice, &framebufferInfo, nullptr, &m_swapChainFrameBuffers[i]) != VK_SUCCESS)
            {
                panic("failed to create freamebuffer!");
            }
        }
    }

    void createTextureImage()
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

    void createTextureImageView()
    {
        m_textureImageView = createImageView(m_textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    void createTextureSampler()
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

    void loadModel()
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

    void createVertexBuffer()
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

    void createIndexBuffer()
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

    void createUniformBuffer()
    {
        VkDeviceSize buffersize = sizeof(UniformBufferObject);

        createBuffer(buffersize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_uniformBuffer, m_uniformBUfferMemory);
    }

    void createDescriptorPool()
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

    void createDescriptorSet()
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

    void createCommandBuffers()
    {
        m_commandBuffers.resize(m_swapChainFrameBuffers.size());

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
                renderPassInfo.framebuffer = m_swapChainFrameBuffers[i];

                renderPassInfo.renderArea.offset = {0, 0};
                renderPassInfo.renderArea.extent = m_swapChainExtent;

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

    void createSemphores()
    {
        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if (vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &m_imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &m_renderFinishedSemaphore) != VK_SUCCESS)
        {
            panic("failed to create semaphores!");
        }
    }

    void recreateSwapChain()
    {
        vkDeviceWaitIdle(m_LogicalDevice); // ����������ʹ���е���Դ

        createSwapChain();
        createImageViews();
        createRenderPass();
        createGraphicsPipeline();
        createDepthResources();
        createFramebuffers();
        createCommandBuffers();
    }

  private: // ���� �ض��� ����


    void cleanupSwapChain()
    {
        vkDestroyImageView(m_LogicalDevice, m_depthImageView, nullptr);
        vkDestroyImage(m_LogicalDevice, m_depthImage, nullptr);
        vkFreeMemory(m_LogicalDevice, m_depthImageMemory, nullptr);

        for (size_t i = 0; i < m_swapChainFrameBuffers.size(); ++i)
        {
            vkDestroyFramebuffer(m_LogicalDevice, m_swapChainFrameBuffers[i], nullptr);
        }

        vkDestroyPipeline(m_LogicalDevice, m_graphicsPipeLine, nullptr);
        vkDestroyPipelineLayout(m_LogicalDevice, m_pipelineLayout, nullptr);
        vkDestroyRenderPass(m_LogicalDevice, m_renderPass, nullptr);

        vkDestroyImageView(m_LogicalDevice, m_textureImageView, nullptr);

        for (size_t i = 0; i < m_swapChainImageViews.size(); ++i)
        {
            vkDestroyImageView(m_LogicalDevice, m_swapChainImageViews[i], nullptr);
        }

        vkDestroySwapchainKHR(m_LogicalDevice, m_swapChain, nullptr);
    }

  private: // mainLoop �׶κ���

    void drawFrame()
    {

        // 1. �����ź��� : creteSemphores() in initVulkan()
        // 2. �ӽ�������ȡͼ��
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(m_LogicalDevice, m_swapChain, std::numeric_limits<uint64_t>::max(), m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
        /*
        vkAcquireNextImageKHR����ǰ��������������ϣ����ȡ��ͼ����߼��豸�ͽ�������
        ����������ָ����ȡ��Чͼ��Ĳ���timeout����λ���롣����ʹ��64λ�޷������ֵ��ֹtimeout��
        ����������������ָ��ʹ�õ�ͬ�����󣬵�presentation���������ͼ��ĳ��ֺ��ʹ�øö������źš�
        ����ǿ�ʼ���Ƶ�ʱ��㡣������ָ��һ���ź���semaphore����դ���������ߡ�����Ŀ���ԣ����ǻ�ʹ��imageAvailableSemaphore��
        ���Ĳ���ָ���������г�Ϊavailable״̬��ͼ���Ӧ���������������������ý�����ͼ������swapChainImages��ͼ��VkImage������ʹ���������ѡ����ȷ�����������
        */

        vkQueueWaitIdle(m_presentQueue);

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

        if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            panic("failed to submit draw command buffer!");
        }

        // 4. ������ύ�� swapchain ������, ��ʾ����Ļ��
        VkPresentInfoKHR presentInfo  = {};
        VkSwapchainKHR   swapChains[] = {m_swapChain};
        {
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores    = signalSemaphores; // ָ����Ҫ�ȴ����ź����� �� VkSubmitInfo һ��

            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains    = swapChains; // ָ���ύ�� target swapchain �� ÿ�� swapchain ������
            presentInfo.pImageIndices  = &imageIndex;

            presentInfo.pResults = nullptr; // ָ��У����ֵ�����ǿ���ֱ��ʹ�� vkQueuePresentKHR() �ķ���ֵ�ж�  Optional
        }

        result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
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

    const VkDebugUtilsMessengerCreateInfoEXT &get_debug_messenger_create_info()
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

    void setup_debug_messenger()
    {
        if (!m_EnableValidationLayers)
            return;

        const VkDebugUtilsMessengerCreateInfoEXT &createInfo = get_debug_messenger_create_info();

        if (CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessengerCallback) != VK_SUCCESS)
        {
            panic("failed to set up debug messenger!");
        }
    }

    void setup_report_callback()
    {
        if (!m_EnableValidationLayers)
            return;

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


        auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(m_instance,
                                                                              "vkCreateDebugReportCallbackEXT");
        if (!func) {
            panic("failed to set up debug callback!", VK_ERROR_EXTENSION_NOT_PRESENT);
        }
        VkResult ret = func(m_instance, &report_cb_create_info, nullptr, &m_debugReportCallback);
        if (VK_SUCCESS != ret) {
            panic("failed to set up debug callback!", ret);
        }
    }

    void pickPhysicalDevice()
    {
        uint32_t count  = 0;
        VkResult result = vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
        NE_ASSERT(count > 0, "Failed to find GPUs with Vulkan support!");

        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

        NE_TRACE("--Physical Device {}", count);

        for (const auto &device : devices)
        {
            std::cout << "----Physical Device-" << device << std::endl;
            // TODO(fix) : no handle for present?
            if (isDeviceSuitable(device))
            {
                m_physicalDevice = device;
                break;
            }
        }

        if (m_physicalDevice == VK_NULL_HANDLE) {
            panic("failed to find a suitable GPU!");
        }
    }

    bool isDeviceSuitable(VkPhysicalDevice device)
    {
        // VkPhysicalDeviceProperties deviceProperties;
        // vkGetPhysicalDeviceProperties(device, &deviceProperties);

        // VkPhysicalDeviceFeatures devicesFeatures;
        // vkGetPhysicalDeviceFeatures(device, &devicesFeatures);

        // return (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        //	&& devicesFeatures.geometryShader;

        QueueFamilyIndices indices = findQueueFamilies(device);

        bool extensionsSupported = checkDeviceExtensionSupport(device);

        bool swapChainAdequate = false;
        if (extensionsSupported)
        {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);

            swapChainAdequate = (!swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty());
        }

        VkPhysicalDeviceFeatures supportedFeatures;
        vkGetPhysicalDeviceFeatures(device, &supportedFeatures);


        return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device)
    {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());


        int i = 0;
        for (const auto &queueFamily : queueFamilies) {
            if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.graphicsFamily = i;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
            if (queueFamily.queueCount > 0 && presentSupport)
            {
                indices.presentFamily = i;
            }

            if (indices.isComplete()) {
                break;
            }
            ++i;
        }


        return indices;
    }


    bool is_validation_layers_supported()
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

    bool checkDeviceExtensionSupport(VkPhysicalDevice device)
    {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        // Global predefine extensions
        std::set<std::string> requiredExtensions(m_DeviceExtensions.begin(), m_DeviceExtensions.end());

        for (const auto &extension : availableExtensions)
        {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device)
    {
        SwapChainSupportDetails details;

        // Capabilities
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

        // Formats
        uint32_t formatCount;
        {
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
            if (formatCount != 0)
            {
                details.formats.resize(formatCount);
                vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
            }
        }


        // PresentModes
        uint32_t presentModeCount;
        {
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
            if (presentModeCount != 0)
            {
                details.presentModes.resize(presentModeCount);
                vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
            }
        }


        return details;
    }

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableForamts)
    {
        // Method 1
        if (availableForamts.size() == 1 && availableForamts[0].format == VK_FORMAT_UNDEFINED)
        {
            return {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        }

        // Method 2
        for (const auto &availableForamt : availableForamts)
        {
            if (availableForamt.format == VK_FORMAT_B8G8R8A8_UNORM && availableForamt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return availableForamt;
            }
        }

        // Fallback : Grede and rank, but ͨ��ѡ���һ����ʽ
        return availableForamts[0];
    }

    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes)
    {
        VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;

        for (const auto &availablePresentMode : availablePresentModes)
        {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
            else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                return availablePresentMode;
            }
        }

        return bestMode;
    }

    // ������Χ
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities)
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        else {
            int width, height;
            m_GLFWState->GetWindowSize(width, height);
            VkExtent2D actualExtent = {(uint32_t)width, (uint32_t)height};

            actualExtent.width = std::max(
                capabilities.minImageExtent.width,
                std::min(capabilities.maxImageExtent.width, actualExtent.width));
            actualExtent.height = std::max(
                capabilities.minImageExtent.height,
                std::min(capabilities.maxImageExtent.height, actualExtent.height));

            return actualExtent;
        }
    }

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

    VkShaderModule createShaderModule(const std::vector<char> &code)
    {
        VkShaderModuleCreateInfo shaderModuleCreateInfo = {};
        {
            shaderModuleCreateInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            shaderModuleCreateInfo.codeSize = code.size();

            shaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
        }

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_LogicalDevice, &shaderModuleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            panic("failed to create shader module");
        }

        return shaderModule;
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

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

        vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE); // �ύ�� graphic queue

        vkQueueWaitIdle(m_graphicsQueue);

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

    VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
    {
        for (VkFormat format : candidates)
        {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &properties);
            /// <properties>
            /// VkFormatProperties �ṹ����������ֶΣ�
            /// 	linearTilingFeatures : ʹ������ƽ�̸�ʽ
            /// 	optimalTilingFeatures : ʹ�����ƽ�̸�ʽ
            /// 	bufferFeatures : ֧�ֻ�����
            ///  ֻ��ǰ��������������صģ����Ǽ��ȡ���ں����� tiling ƽ�̲�����
            /// </properties>
            if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features) {
                return format;
            }
            else if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features) {
                return format;
            }
        }

        panic("failed to find supported format!");
        return VK_FORMAT_UNDEFINED;
    }

    VkFormat findDepthFormat()
    {
        return findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                                   VK_IMAGE_TILING_OPTIMAL,
                                   VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }


    // foarmat �Ƿ�֧�� stencil test
    bool hasStencilComponent(VkFormat format)
    {
        return format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

  private: // static ����
};



struct App
{
    GLFWState   m_GLFWState;
    VulkanState m_VulkanState;


    struct FTime
    {
        static float Now()
        {
            if constexpr (true) // use GLFW create window
            {
                return (float)glfwGetTime();
            }
        }
    };

    void Init()
    {
        neon::Logger::Init();
        m_GLFWState.Init();
        m_VulkanState.Init(&m_GLFWState);
        m_GLFWState.OnKeyboardInput.AddStatic([](GLFWwindow *window, int key, int scancode, int action, int mods) {
        });
    }

    void Uninit()
    {
        m_VulkanState.Uninit();
        m_GLFWState.Uninit();
    }

    void Run()
    {
        float last_time = FTime::Now();
        while (!ShouldClose())
        {
            float time = FTime::Now();
            float dt   = time - last_time;
            last_time  = time;
            m_GLFWState.OnUpdate();
            m_VulkanState.PreUpdate();
            m_VulkanState.PostUpdate();
        }
    }

    bool ShouldClose()
    {
        if constexpr (true) // use GLFW create window
        {
            return glfwWindowShouldClose(m_GLFWState.m_Window);
        }
    }
};


int main()
{
    printf("hello world\n");

    auto app = new App();

    app->Init();
    app->Run();
    app->Uninit();

    delete app;
}
