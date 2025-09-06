#pragma once

#include "Render/Render.h"
#include <Core/Base.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <ImGui.h>
#include <imgui_impl_sdl3.h>

#include <imgui_impl_sdlgpu3.h>

#define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
#include <imgui_impl_vulkan.h>

#include "Core/Event.h"
#include "Core/Log.h"

struct VulkanRender;
struct VulkanRenderPass;
namespace ya
{

struct ImguiState
{

    struct VulkanImpl
    {

        static void init(IRender *render, VulkanRenderPass *renderPass);
    };

    struct SDLGPUImpl
    {};

    ImDrawData *drawData = nullptr;

    // Initialize ImGui self
    void init()
    {

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO *io = &ImGui::GetIO();
        io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
    }

    // Initialize ImGui with SDL and SDLGPU backends
    void init(SDL_Window *window, SDL_GPUDevice *device)
    {
        init();

        ImGui_ImplSDL3_InitForSDLGPU(window);
        SDL_WaitForGPUSwapchain(device, window);
        auto swapChianFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
        YA_CORE_DEBUG("Swapchain format: {}, device: {}, window: {}",
                      static_cast<int>(swapChianFormat),
                      (uintptr_t)device,
                      (uintptr_t)window);
        if (swapChianFormat == SDL_GPU_TEXTUREFORMAT_INVALID) {
            YA_CORE_ERROR("Failed to get swapchain texture format: {}",
                          SDL_GetError());
        }
    }

    //  Initialize ImGui with Vulkan backend
    void init(SDL_Window *window, const ImGui_ImplVulkan_InitInfo &initInfo)
    {
        init();
        ImGui_ImplSDL3_InitForVulkan(window);
        ImGui_ImplVulkan_Init(const_cast<ImGui_ImplVulkan_InitInfo *>(&initInfo));
    }

    // Begin new ImGui frame
    void beginFrame()
    {
        // ImGui_ImplSDLGPU3_NewFrame();

        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
    }

    void endFrame() { ImGui::EndFrame(); }

    bool render()
    {
        // here to manage all imgui elements into drawData (cmds, resources:
        // vertexBuffer)
        ImGui::Render();
        drawData = ImGui::GetDrawData();
        // should after check minimized and swapchain texture?
        const bool bMinimized =
            drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f;
        return bMinimized;
    }

    // only sdlgpu need to be call
    void prepareDrawdata(SDL_GPUCommandBuffer *cmdBuffer)
    {
        // in sdlGPU, should be upload data?
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData,
                                          (SDL_GPUCommandBuffer *)cmdBuffer);

        // we could control the time for upload textures?
        // ImGui_ImplVulkan_AddTexture()
    }

    // sdlGPU
    // Prepare and render ImGui draw data
    void submit(SDL_GPUCommandBuffer *commandBuffer,
                SDL_GPURenderPass    *renderpass)
    {
        if (drawData && drawData->CmdListsCount > 0) {
            ImGui_ImplSDLGPU3_RenderDrawData(drawData, commandBuffer, renderpass);
        }
    }
    // vulkan
    void submit(VkCommandBuffer cmdBuf, VkPipeline pipeline = VK_NULL_HANDLE)
    {
        if (drawData && drawData->CmdListsCount > 0) {
            ImGui_ImplVulkan_RenderDrawData(drawData, cmdBuf, pipeline);
        }
    }

    // Shutdown ImGui
    void shutdown()
    {
        ImGui_ImplSDL3_Shutdown();
        ImGui_ImplVulkan_Shutdown();
        ImGui::DestroyContext();
    }

    EventProcessState processEvents(const SDL_Event &event)
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        // [If using SDL_MAIN_USE_CALLBACKS: call ImGui_ImplSDL3_ProcessEvent() from your SDL_AppEvent() function]
        ImGui_ImplSDL3_ProcessEvent(&event);

        auto io = &ImGui::GetIO();

        // 检查鼠标事件
        bool isMouseEvent = (event.type == SDL_EVENT_MOUSE_MOTION ||
                             event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                             event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                             event.type == SDL_EVENT_MOUSE_WHEEL);

        // 检查键盘事件
        // bool isKeyEvent = (event.type == SDL_EVENT_KEY_DOWN ||
        //                    event.type == SDL_EVENT_KEY_UP);

        // 检查文本输入事件
        // bool isTextEvent = (event.type == SDL_EVENT_TEXT_EDITING ||
        //                     event.type == SDL_EVENT_TEXT_INPUT);

        if ((io->WantCaptureMouse && isMouseEvent)
            // (io->WantCaptureKeyboard && isKeyEvent) ||
            // (io->WantTextInput && isTextEvent)
        ) {
            return EventProcessState::Handled;
        }

        return EventProcessState::Continue;
    }

    // Check if ImGui is currently using mouse or keyboard inputs
    bool isWantInput()
    {
        ImGuiIO &io = ImGui::GetIO();
        return io.WantCaptureMouse || io.WantCaptureKeyboard;
    }
};

} // namespace ya