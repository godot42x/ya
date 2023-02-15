

#include <iostream>
#include <locale>
#include <stdexcept>
#include <string>

#include <GLX/glinternal/Shader.h>
#include <GLX/ownkit/kit.h>

#include <spdlog/spdlog.h>

const int WIN_WIDTH = 800;
const int WIN_HEIGHT = 600;

int main(int argc, char **argv)
{
    using std::cout, std::endl;

    if (GL_TRUE != glfwInit())
    {
        throw std::runtime_error("Failed to init glfw");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *p_Window;
    p_Window = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, "GLX", nullptr, nullptr);
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
    cout << glGetString(GL_VERSION) << endl;

    // Config
    {
        glfwSwapInterval(1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glViewport(0, 0, WIN_WIDTH, WIN_HEIGHT);
        glfwSetFramebufferSizeCallback(
            p_Window, [](GLFWwindow *pWindow, int width, int height) { glViewport(0, 0, width, height); });
    }

    auto processInput = [](GLFWwindow *pWindow) {
        if (glfwGetKey(pWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(pWindow, 1);
        }
    };

    while (!glfwWindowShouldClose(p_Window))
    {
        processInput(p_Window);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.2, 0.3, 0.3, 1);

        glfwSwapBuffers(p_Window);
        glfwPollEvents();
    }

    glfwTerminate();

    return 0;
}
