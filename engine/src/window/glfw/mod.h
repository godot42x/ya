/**
 * @ Author: godot42
 * @ Create Time: 2024-09-22 06:00:07
 * @ Modified by: @godot42
 * @ Modified time: 2024-09-22 06:17:23
 * @ Description:
 */

#pragma once

#include "../window.h"

namespace neon::platform {



class GLFWWindow : public Window
{
  public:
    void Init()
    {
        if (GL_TRUE != glfwInit())
        {
            throw std::runtime_error("Failed to init glfw");
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        p_Window = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, "Gloria", nullptr, nullptr);
        if (!p_Window)
        {
            glfwTerminate();
            throw std::runtime_error("Failed to create window");
        }

        glfwMakeContextCurrent(p_Window);

        // glew
        if (glewInit() != GLEW_OK)
        {
            throw std::runtime_error("Failed to initialize GLEW!!");
        }
        // glad
        // if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        //{
        //    throw std::runtime_error("Failed to initialize GLAD!!");
        //}

        LINFO("Gl core init success!");
        auto verInfo = glGetString(GL_VERSION);
        LINFO(reinterpret_cast<const char *>(verInfo));
    }
};

} // namespace neon::platform