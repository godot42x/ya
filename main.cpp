#include <GLFW/glfw3.h>
#include <cstdlib>
#include <iostream>


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