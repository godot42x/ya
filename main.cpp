#include <GLFW/glfw3.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include <ratio>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

void panic(const char *msg, int code = 1)
{
    std::cerr << "[ PANIC ] --> " << msg << std::endl;
    std::exit(code);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void                                       *pUserData)
{
    std::cerr << "[ Validation Layer ] --> " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}

struct VulkanState
{

  private:
    bool bEnableValidationLayers = true;

    const std::vector<const char *> m_ValidationLayers = {
        "VK_LAYER_KHRONOS_validation",
    };
    VkInstance m_Instance;


  private:
    void create_instance(const std::string &app_name)
    {
        VkApplicationInfo app_info{
            .sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext            = nullptr,
            .pApplicationName = app_name.c_str(),
            .pEngineName      = "Neon",
            .apiVersion       = VK_API_VERSION_1_3};

        std::vector<const char *> extensions = get_required_extensions();

        VkInstanceCreateInfo instance_create_info{
            .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext                   = nullptr,
            .pApplicationInfo        = &app_info,
            .enabledLayerCount       = 0,
            .ppEnabledLayerNames     = nullptr,
            .enabledExtensionCount   = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data()};


        VkDebugUtilsMessengerCreateInfoEXT debug_create_info{
            .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = VulkanDebugMessengerCallback,
        };
        if (bEnableValidationLayers) {
            instance_create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            instance_create_info.ppEnabledLayerNames   = m_ValidationLayers.data();
            instance_create_info.pNext                 = &debug_create_info; // add debugMessengerCreateInfo to linklist
        }
        else {
            instance_create_info.enabledLayerCount = 0;
            instance_create_info.pNext             = nullptr;
        }

        VkResult result = vkCreateInstance(&instance_create_info, nullptr, &m_Instance);
        if (result != VK_SUCCESS) {
            panic("failed to create instance!");
        }
        // VkResult ret = vkCreateDebugUtilsMessengerEXT(m_Instance,
        //                                               &debug_create_info,
        //                                               nullptr,
        //                                               &VulkanDebugMessengerCallback);
    };

    std::vector<const char *> get_required_extensions()
    {
        std::vector<const char *> extensions;

        uint32_t     glfwExtensionsCount = 0;
        const char **glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);

        // TODO
        std::cout << "glfwGetRequiredInstanceExtensions: " << glfwExtensions << std::endl;

        for (unsigned int i = 0; i < glfwExtensionsCount; ++i) {
            extensions.push_back(glfwExtensions[i]);
        }
        if (bEnableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        return extensions;
    }

    void populate_debug_mssenger_create_info(VkDebugUtilsMessengerCreateInfoEXT &debugCreatInfo)
    {
    }
};

int main()
{
    printf("hello world\n");

    if (GLFW_TRUE != glfwInit())
    {
        // throw std::runtime_error("Failed to init glfw");
        exit(1);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    auto window = glfwCreateWindow(1024, 768, "Neon", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        // throw std::runtime_error("Failed to create window");
        exit(2);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);


    glfwSetWindowCloseCallback(window, [](GLFWwindow *window) {});
    glfwSetKeyCallback(window, [](GLFWwindow *window, int key, int scancode, int action, int mods) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    });


    float last_time = (float)glfwGetTime();
    while (!glfwWindowShouldClose(window))
    {
        float time = (float)glfwGetTime();
        float dt   = time - last_time;
        last_time  = time;
        glfwPollEvents();
    }



    glfwTerminate();
}