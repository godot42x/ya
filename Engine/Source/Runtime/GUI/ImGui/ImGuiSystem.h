#pragma once

#ifndef IMGUI_SDL3_GPU
    #define IMGUI_SDL3_GPU 0
#endif

#include "Core/Base.h"
#include "Core/Event.h"
#include "Core/Log.h"
#include "Render/Core/RenderPass.h"
#include "Render/Render.h"

#include <SDL3/SDL.h>

#if IMGUI_SDL3_GPU
    #include <SDL3/SDL_gpu.h>
#endif

#ifndef IMGUI_ENABLE_FREETYPE
    #define IMGUI_ENABLE_FREETYPE
#endif
#ifndef IMGUI_USE_WCHAR32
    #define IMGUI_USE_WCHAR32
#endif
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_impl_vulkan.h>

#include <ImGuizmo.h>

namespace ya
{

struct IImageView;
struct Sampler;

struct ImGuiManager
{
  private:
    ImDrawData* _drawData    = nullptr;
    bool        _initialized = false;
    bool        bBlockEvents = false;

  public:
    ImGuiManager()                               = default;
    ImGuiManager(const ImGuiManager&)            = delete;
    ImGuiManager(ImGuiManager&&)                 = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;
    ImGuiManager& operator=(ImGuiManager&&)      = delete;
    ~ImGuiManager()                              = default;

    static ImGuiManager& get();

    void init(IRender* render, IRenderPass* renderPass);
    void initVulkan(SDL_Window* window, IRender* render, IRenderPass* renderPass);

#if IMGUI_SDL3_GPU
    void initSDLGPU(SDL_Window* window, SDL_GPUDevice* device);
#endif

    void shutdown();
    void beginFrame();
    void endFrame();
    bool render();
    void submitVulkan(VkCommandBuffer cmdBuf, VkPipeline pipeline = VK_NULL_HANDLE);

#if IMGUI_SDL3_GPU
    void submitSDLGPU(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderpass);
#endif

    EventProcessState processEvents(const SDL_Event& event);
    EventProcessState processEvent(const Event& event);
    bool              isWantInput() const;
    void              initImGuiCore();
    void              setBlockEvents(bool block) { bBlockEvents = block; }

    static void* addTexture(IImageView* imageView, Sampler* sampler, EImageLayout::T layout = EImageLayout::ShaderReadOnlyOptimal);
    static void  removeTexture(void* textureID);
    static void  setGizmoRect(float x, float y, float width, float height);

    bool onRenderGUI();
};

using ImguiState = ImGuiManager;

namespace ImGuiHelper
{

void BeginFrame();

bool Image(IImageView*        imageView,
           Sampler*           sampler,
           const std::string& alt,
           const ImVec2&      size,
           const ImVec2&      uv0    = ImVec2(0, 0),
           const ImVec2&      uv1    = ImVec2(1, 1),
           const ImVec4&      tint   = ImVec4(1, 1, 1, 1),
           const ImVec4&      border = ImVec4(0, 0, 0, 0));

void ClearImageCache();

} // namespace ImGuiHelper

struct ImGuiStyleScope
{
    int varCount   = 0;
    int colorCount = 0;

    ImGuiStyleScope() = default;
    ~ImGuiStyleScope()
    {
        if (varCount > 0) ImGui::PopStyleVar(varCount);
        if (colorCount > 0) ImGui::PopStyleColor(colorCount);
    }

    void pushVar(ImGuiStyleVar idx, const ImVec2& v)
    {
        ImGui::PushStyleVar(idx, v);
        ++varCount;
    }

    void pushVar(ImGuiStyleVar idx, float v)
    {
        ImGui::PushStyleVar(idx, v);
        ++varCount;
    }

    void pushColor(ImGuiCol idx, const ImVec4& v)
    {
        ImGui::PushStyleColor(idx, v);
        ++colorCount;
    }
};

} // namespace ya
