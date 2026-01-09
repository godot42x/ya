#pragma once

#include "Render/Core/RenderPass.h"
#include "Render/Render.h"
#include <Core/Base.h>


#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <ImGui.h>
#include <imgui_impl_sdl3.h>

#include <imgui_impl_sdlgpu3.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>


#include "Core/Event.h"
#include "Core/Log.h"

namespace ya
{

struct IImageView;
struct Sampler;

/**
 * @brief ImGuiManager - Manages ImGui lifecycle and rendering
 *
 * Responsibilities:
 * - Initialize ImGui with SDL and backend (Vulkan/SDLGPU)
 * - Handle frame begin/end/render cycle
 * - Process SDL events and determine if ImGui captured input
 * - Submit ImGui draw commands to command buffer
 */
struct ImGuiManager
{
  private:
    ImDrawData *_drawData    = nullptr;
    bool        _initialized = false;
    bool        bBlockEvents = false;



  public:

    ImGuiManager()                                = default;
    ImGuiManager(const ImGuiManager &)            = delete;
    ImGuiManager(ImGuiManager &&)                 = delete;
    ImGuiManager &operator=(const ImGuiManager &) = delete;
    ImGuiManager &operator=(ImGuiManager &&)      = delete;
    ~ImGuiManager()                               = default;

    static ImGuiManager &get();

    /**
     * @brief Initialize ImGui with the appropriate backend based on render API
     * @param render IRender instance for backend initialization
     * @param renderPass The render pass to use for ImGui rendering
     */
    void init(IRender *render, IRenderPass *renderPass);

    /**
     * @brief Initialize ImGui with Vulkan backend
     * @param window SDL window handle
     * @param render IRender instance for backend initialization
     * @param renderPass The render pass to use for ImGui rendering
     */
    void initVulkan(SDL_Window *window, IRender *render, IRenderPass *renderPass);

    /**
     * @brief Initialize ImGui with SDLGPU backend
     */
    void initSDLGPU(SDL_Window *window, SDL_GPUDevice *device);

    /**
     * @brief Shutdown ImGui and cleanup resources
     */
    void shutdown();


    void beginFrame();
    void endFrame();

    /**
     * @brief Render ImGui and generate draw data
     * @return true if minimized (no rendering needed), false otherwise
     */
    bool render();

    /**
     * @brief Submit ImGui draw commands to Vulkan command buffer
     */
    void submitVulkan(VkCommandBuffer cmdBuf, VkPipeline pipeline = VK_NULL_HANDLE);

    /**
     * @brief Submit ImGui draw commands to SDLGPU
     */
    void submitSDLGPU(SDL_GPUCommandBuffer *commandBuffer, SDL_GPURenderPass *renderpass);

    /**
     * @brief Process SDL events and determine if ImGui captured them
     * @return EventProcessState::Handled if ImGui captured the event, Continue otherwise
     */
    EventProcessState processEvents(const SDL_Event &event);
    EventProcessState processEvent(const Event &event);

    /**
     * @brief Check if ImGui wants to capture input
     */
    bool isWantInput() const;

    void initImGuiCore();

    void setBlockEvents(bool block) { bBlockEvents = block; }


    /**
     * @brief Create ImGui descriptor set for a texture (platform-agnostic wrapper)
     * @param imageView Platform image view handle (VkImageView)
     * @param sampler Platform sampler handle (VkSampler)
     * @param layout Image layout (default: SHADER_READ_ONLY_OPTIMAL)
     * @return ImTextureID (descriptor set handle)
     */
    static void *addTexture(IImageView *imageView, Sampler *sampler, EImageLayout::T layout = EImageLayout::ShaderReadOnlyOptimal);

    /**
     * @brief Remove ImGui descriptor set for a texture
     * @param textureID ImTextureID to remove
     */
    static void removeTexture(void *textureID);

    /**
     * @brief Begin ImGuizmo frame (should be called after ImGui::Begin)
     * Sets up ImGuizmo draw list and rect for the current window
     */
    static void beginGizmoFrame();

    /**
     * @brief Manipulate a transform matrix with ImGuizmo
     * @param view View matrix
     * @param projection Projection matrix
     * @param operation Gizmo operation (Translate/Rotate/Scale)
     * @param mode Gizmo mode (Local/World)
     * @param matrix Transform matrix to manipulate (in/out)
     * @return true if the gizmo was used and matrix was modified
     */
    static bool manipulate(
        const float *view,
        const float *projection,
        ImGuizmo::OPERATION operation,
        ImGuizmo::MODE mode,
        float *matrix
    );

    /**
     * @brief Set ImGuizmo rect for the current viewport
     * @param x X position
     * @param y Y position
     * @param width Width
     * @param height Height
     */
    static void setGizmoRect(float x, float y, float width, float height);
};

// Legacy alias for backward compatibility
using ImguiState = ImGuiManager;

} // namespace ya