#pragma once

#include "GLFW/glfw3.h"
#include "base.h"
#include "delegate.h"
#include "log.h"


struct GLFWState : public Layer
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
        NE_TRACE("glfwGetRequiredInstanceExtensions: ");
        for (unsigned int i = 0; i < count; ++i) {
            NE_TRACE("  {}", glfwExtensions[i]);
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
