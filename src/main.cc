#include <pch/gl.h>
#include <pch/std.h>


#include <glinternal/core.hpp>
#include <logx/spdx.hpp>



int main(int argc, char **argv)
{
    logx::Loggerx::Instance().InitConsoleLogger("Gloria", "GLORIA_ERR").SetLogLvel(spdlog::level::trace);

    glinternal::Gloria Context;
    Context.Init();

    // Config
    {
        glfwSwapInterval(1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glViewport(0, 0, WIN_WIDTH, WIN_HEIGHT);
        glfwSetFramebufferSizeCallback(
            Context.Window(),
            [](GLFWwindow *pWindow, int width, int height) { glViewport(0, 0, width, height); });
    }

    auto processInput = [](GLFWwindow *pWindow) {
        if (glfwGetKey(pWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(pWindow, 1);
        }
    };

    while (!glfwWindowShouldClose(Context.Window()))
    {
        processInput(Context.Window());
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.2, 0.3, 0.3, 1);

        glfwSwapBuffers(Context.Window());
        glfwPollEvents();
    }

    glfwTerminate();

    return 0;
}
