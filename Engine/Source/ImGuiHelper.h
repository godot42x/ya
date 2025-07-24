#pragma once

#include <Core/Base.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <ImGui.h>
#include <imgui_impl_sdl3.h>
#if USE_VULKAN
    #include <imgui_impl_vulkan.h>
#else
    #include <imgui_impl_sdlgpu3.h>
#endif


#include "Core/App/App.h"
#include "Core/Event.h"
#include "Core/Log.h"


namespace Neon
{

struct ImguiState
{
    ImDrawData *drawData = nullptr;

    // Initialize ImGui with SDL and SDLGPU backends
    void init(SDL_GPUDevice *device, SDL_Window *window)
    {
        NE_CORE_ASSERT(device != nullptr && window != nullptr, "SDL GPU device or window is null");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO *io = &ImGui::GetIO();
        io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        ImGui_ImplSDL3_InitForVulkan(window);

#if USE_SDL3_GPU
        ImGui_ImplSDL3_InitForSDLGPU(window);
#endif

        SDL_WaitForGPUSwapchain(device, window);
        auto swapChianFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
        NE_CORE_DEBUG("Swapchain format: {}, device: {}, window: {}", static_cast<int>(swapChianFormat), (uintptr_t)device, (uintptr_t)window);
        if (swapChianFormat == SDL_GPU_TEXTUREFORMAT_INVALID) {
            NE_CORE_ERROR("Failed to get swapchain texture format: {}", SDL_GetError());
        }
#if USE_VULKAN
        // ImGui_ImplVulkan_InitInfo initInfo{
        //     .MSAASamples = SDL_GPU_SAMPLECOUNT_1,
        // };
        // ImGui_ImplVulkan_Init(&initInfo);
#else
        ImGui_ImplSDLGPU3_InitInfo info{
            .Device            = device,
            .ColorTargetFormat = swapChianFormat,
            .MSAASamples       = SDL_GPU_SAMPLECOUNT_1,
        };
        ImGui_ImplSDLGPU3_Init(&info);
#endif
    }

    // Begin new ImGui frame
    void beginFrame()
    {
#if USE_VULKAN
#else
// ImGui_ImplSDLGPU3_NewFrame();
#endif

        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    bool render(SDL_GPUCommandBuffer *commandBuffer)
    {
        ImGui::Render();
        drawData = ImGui::GetDrawData();
        // should after check minimized and swapchain texture?
        const bool bMinimized = drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f;
        return bMinimized;
    }

    void prepareDrawData(SDL_GPUCommandBuffer *commandBuffer)
    {
#if USE_VULKAN
#else
        Imgui_ImplSDLGPU3_PrepareDrawData(drawData, commandBuffer);
#endif
    }


    // Prepare and render ImGui draw data
    void draw(SDL_GPUCommandBuffer *commandBuffer, SDL_GPURenderPass *renderpass)
    {
        if (drawData && drawData->CmdListsCount > 0) {
#if 0 
            ImGui_ImplSDLGPU3_RenderDrawData(drawData, commandBuffer, renderpass);
#endif
        }
    }

    // Shutdown ImGui
    void shutdown()
    {
        ImGui_ImplSDL3_Shutdown();
#if 0
        ImGui_ImplSDLGPU3_Shutdown();
#endif
        ImGui::DestroyContext();
    }

    // Process SDL events for ImGui
    EventProcessState processEvents(const SDL_Event &event)
    {
        ImGui_ImplSDL3_ProcessEvent(&event);

        // TODO: dkw the ImGui_ImplSDL3_ProcessEvent return value is always true
        return EventProcessState::Continue;
    }

    // Check if ImGui is currently using mouse or keyboard inputs
    bool isWantInput()
    {
        ImGuiIO &io = ImGui::GetIO();
        return io.WantCaptureMouse || io.WantCaptureKeyboard;
    }
};
} // namespace Neon
