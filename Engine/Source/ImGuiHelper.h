#pragma once

#include "Render/Core/RenderPass.h"
#include "Render/Render.h"
#include <Core/Base.h>


#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <ImGui.h>
#include <imgui_impl_sdl3.h>

#include <imgui_impl_sdlgpu3.h>

// Forward declarations for Vulkan types (avoid including vulkan headers)
#if USE_VULKAN
typedef struct VkCommandBuffer_T *VkCommandBuffer;
typedef struct VkPipeline_T      *VkPipeline;
#define VK_NULL_HANDLE 0
#endif

#include "Core/Event.h"
#include "Core/Log.h"

namespace ya
{

/**
 * @brief ImGuiManager - Manages ImGui lifecycle and rendering
 *
 * Responsibilities:
 * - Initialize ImGui with SDL and backend (Vulkan/SDLGPU)
 * - Handle frame begin/end/render cycle
 * - Process SDL events and determine if ImGui captured input
 * - Submit ImGui draw commands to command buffer
 */
class ImGuiManager
{
  public:
    ImGuiManager()  = default;
    ~ImGuiManager() = default;

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

    /**
     * @brief Begin a new ImGui frame
     */
    void beginFrame();

    /**
     * @brief End the current ImGui frame
     */
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

    /**
     * @brief Check if ImGui wants to capture input
     */
    bool isWantInput() const;

  private:
    ImDrawData *_drawData    = nullptr;
    bool        _initialized = false;

    void initImGuiCore();
};

// Legacy alias for backward compatibility
using ImguiState = ImGuiManager;

} // namespace ya