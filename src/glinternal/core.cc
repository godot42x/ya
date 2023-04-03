#include <minimal.h>

#include "glinternal/core.h"
#include "spdlog/spdlog.h"

#include <logx/spdx.h>



namespace glinternal {


void glinternal::Gloria::Init()
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
    auto verInfo = glGetString(GL_VERSION);
    LINFO(reinterpret_cast<const char *>(verInfo));
}

} // namespace glinternal
