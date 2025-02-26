
#include "delegate.h"



#include "C:\Users\norm\1\craft\Neon\engine\src\utils\path.h"
#include "engine/src/utils/path.h"
#include "glfw_state.h"
#include "vulkan_state.h"



struct UniformBufferObject
{};
enum class ERenderAPI;

namespace std
{
const char *to_string(VkDebugUtilsMessageSeverityFlagBitsEXT bit);
} // namespace std



struct App;



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

} // namespace std

// #define STB_IMAGE_IMPLEMENTATION
// #include<stb/stb_iamge.h>
// #include<tiny_obj_loader.h>



// const int WIDTH  = 800;
// const int HEIGHT = 600;

// const std::string MODEL_PATH   = "models/monkey.obj";
// const std::string TEXTURE_PATH = "textures/texture.jpg";



// const std::vector<Vertex> m_vertices = {
//     // ���� �� rgb �� texture �Ľ����� ����Opengl���ƶ���ת��y�ᣬ��ʱ�����룬˳ʱ��������ͼ���ۣ�
//     {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
//     {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
//     {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
//     {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},

//     {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
//     {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
//     {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
//     {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}};

// const std::vector<uint16_t> m_indices = {
//     0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};


/***************************************************************************************************/



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
        utils::project_locate::init({"engine", "example"});
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
            m_VulkanState.OnUpdate();
            m_VulkanState.OnPostUpdate();
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
