
#include <Gloria/glinternal/Shader.h>
#include <Gloria/glinternal/core.h>
#include <cstdio>
#include <filesystem>



#include <GL/glew.h>
#include <GLFW/glfw3.h>



#include <Gloria/logx/spdx.h>
#include <iostream>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <math.h>

#include <libgen.h>


static ImGuiContext *g_ImGuiCtx;

static void initImgui(GLFWwindow *glWindow)
{
    IMGUI_CHECKVERSION();
    g_ImGuiCtx = ImGui::CreateContext();

    // key input
    auto io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // docking without hold shift
    io.ConfigDockingWithShift = false;

    {
        auto cascadia_font_path = std::filesystem::relative("../res/ttf/Cascadia.ttf");
        LDEBUG("Font path in relative : {}", cascadia_font_path.c_str());
        auto cascadia_font = io.Fonts->AddFontFromFileTTF(cascadia_font_path.c_str(), 20.f);
        if (!cascadia_font) {
            LWARN("Loadding font cascadia from {} failed", cascadia_font_path.c_str());
        }
        // It's a stack , so need add default font after our customer fon
        io.FontDefault = cascadia_font;
        io.Fonts->AddFontDefault();
    }



    if (true)
        ImGui::StyleColorsDark();
    else
        ImGui::StyleColorsLight();

    ImGui_ImplGlfw_InitForOpenGL(glWindow, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}


int main(int argc, char **argv)
{

    logx::Loggerx::Instance()
        .InitConsoleLogger("Gloria", "GLORIA_ERR")
        .SetLogLvel(spdlog::level::trace);

    // LERROR("Setting glfw err callback {} {}", "test", 123);
    glfwSetErrorCallback([](auto error, auto descri) {
        LERROR("GLFW: {} {}\n", error, descri);
    });

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

    initImgui(Context.Window());
    LDEBUG("Init imgui success!");
    bool show_demo_window = true;

    // glinternal::Shader shader("vert", "frag");

    auto processInput = [](GLFWwindow *pWindow) {
        if (glfwGetKey(pWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(pWindow, 1);
        }
    };


    while (!glfwWindowShouldClose(Context.Window()))
    {
        glfwPollEvents();
        processInput(Context.Window());

        // Start imgui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        {

            ImGui::Begin("Hello world");
            ImGui::End();
        }

        ImGui::Render();

        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.2, 0.3, 0.3, 1);


        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(Context.Window());
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(Context.Window());
    glfwTerminate();

    return 0;
}
