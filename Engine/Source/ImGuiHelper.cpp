#include "ImGuiHelper.h"

#include <vulkan/vulkan.h>

#define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
#include <imgui_impl_vulkan.h>

#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Core/Log.h"

namespace ya
{

void ImGuiManager::initImGuiCore()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO* io = &ImGui::GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
}

void ImGuiManager::init(IRender* render, IRenderPass* renderPass)
{
    YA_CORE_ASSERT(!_initialized, "ImGuiManager already initialized");
    
    ERenderAPI::T api = render->getAPI();
    
    switch (api) {
    case ERenderAPI::Vulkan:
    {
        SDL_Window* window = render->getNativeWindow<SDL_Window*>();
        initVulkan(window, render, renderPass);
    } break;
    
    case ERenderAPI::None:
    case ERenderAPI::OpenGL:
    case ERenderAPI::DirectX12:
    case ERenderAPI::Metal:
    default:
        YA_CORE_ERROR("ImGui backend not implemented for API: {}", static_cast<int>(api));
        break;
    }
}

void ImGuiManager::initVulkan(SDL_Window* window, IRender* render, IRenderPass* renderPass)
{
    YA_CORE_ASSERT(!_initialized, "ImGuiManager already initialized");
    
    initImGuiCore();
    
    auto* vkRender = dynamic_cast<VulkanRender*>(render);
    YA_CORE_ASSERT(vkRender, "Render must be VulkanRender for Vulkan backend");
    
    auto& queue = vkRender->getGraphicsQueues()[0];
    
    ImGui_ImplVulkan_InitInfo initInfo{
        .ApiVersion     = vkRender->getApiVersion(),
        .Instance       = vkRender->getInstance(),
        .PhysicalDevice = vkRender->getPhysicalDevice(),
        .Device         = vkRender->getDevice(),
        .QueueFamily    = queue.getFamilyIndex(),
        .Queue          = queue.getHandle(),
        .DescriptorPool = nullptr,
        .RenderPass     = renderPass->getHandleAs<VkRenderPass>(),
        .MinImageCount  = 2,
        .ImageCount     = vkRender->getSwapchainImageCount(),
        .MSAASamples    = VK_SAMPLE_COUNT_1_BIT,
        .PipelineCache = nullptr,
        .Subpass       = 0,
        .DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE,
        .UseDynamicRendering = false,
#ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
        .PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfoKHR{},
#endif
        .Allocator       = vkRender->getAllocator(),
        .CheckVkResultFn = [](VkResult err) {
            if (err != VK_SUCCESS) {
                YA_CORE_ERROR("Vulkan error in ImGui: {}", static_cast<int>(err));
            }
        },
        .MinAllocationSize = static_cast<VkDeviceSize>(1024 * 1024),
    };
    
    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_Init(&initInfo);
    
    _initialized = true;
    YA_CORE_INFO("ImGuiManager initialized with Vulkan backend");
}

void ImGuiManager::initSDLGPU(SDL_Window* window, SDL_GPUDevice* device)
{
    YA_CORE_ASSERT(!_initialized, "ImGuiManager already initialized");
    
    initImGuiCore();
    
    ImGui_ImplSDL3_InitForSDLGPU(window);
    SDL_WaitForGPUSwapchain(device, window);
    auto swapChainFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
    YA_CORE_DEBUG("Swapchain format: {}, device: {}, window: {}",
                  static_cast<int>(swapChainFormat),
                  (uintptr_t)device,
                  (uintptr_t)window);
    if (swapChainFormat == SDL_GPU_TEXTUREFORMAT_INVALID) {
        YA_CORE_ERROR("Failed to get swapchain texture format: {}",
                      SDL_GetError());
    }
    
    _initialized = true;
    YA_CORE_INFO("ImGuiManager initialized with SDLGPU backend");
}

void ImGuiManager::shutdown()
{
    if (!_initialized) {
        return;
    }
    
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    
    _initialized = false;
    YA_CORE_INFO("ImGuiManager shutdown");
}

void ImGuiManager::beginFrame()
{
    YA_CORE_ASSERT(_initialized, "ImGuiManager not initialized");
    
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::endFrame()
{
    ImGui::EndFrame();
}

bool ImGuiManager::render()
{
    ImGui::Render();
    _drawData = ImGui::GetDrawData();
    
    // Check if minimized
    const bool bMinimized =
        _drawData->DisplaySize.x <= 0.0f || _drawData->DisplaySize.y <= 0.0f;
    return bMinimized;
}

void ImGuiManager::submitVulkan(VkCommandBuffer cmdBuf, VkPipeline pipeline)
{
    if (_drawData && _drawData->CmdListsCount > 0) {
        ImGui_ImplVulkan_RenderDrawData(_drawData, cmdBuf, pipeline);
    }
}

void ImGuiManager::submitSDLGPU(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderpass)
{
    if (_drawData && _drawData->CmdListsCount > 0) {
        ImGui_ImplSDLGPU3_RenderDrawData(_drawData, commandBuffer, renderpass);
    }
}

EventProcessState ImGuiManager::processEvents(const SDL_Event& event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);
    
    auto io = &ImGui::GetIO();
    
    // Check mouse events
    bool isMouseEvent = (event.type == SDL_EVENT_MOUSE_MOTION ||
                         event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                         event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                         event.type == SDL_EVENT_MOUSE_WHEEL);
    
    if (io->WantCaptureMouse && isMouseEvent) {
        return EventProcessState::Handled;
    }
    
    return EventProcessState::Continue;
}

bool ImGuiManager::isWantInput() const
{
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

} // namespace ya
