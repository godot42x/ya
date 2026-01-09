#include "ImGuiHelper.h"
#include "Render/Core/Image.h"

#include <vulkan/vulkan.h>

#define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
#include <imgui_impl_vulkan.h>

#include "Core/Log.h"
#include "Platform/Render/Vulkan/VulkanRender.h"


namespace ya
{

void ImGuiManager::initImGuiCore()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO *io = &ImGui::GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();

    auto colors               = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4{0.1f, 0.105f, 0.11f, 1.0f};

    // Headers
    colors[ImGuiCol_Header]        = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
    colors[ImGuiCol_HeaderHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
    colors[ImGuiCol_HeaderActive]  = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

    // Buttons
    colors[ImGuiCol_Button]        = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
    colors[ImGuiCol_ButtonHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
    colors[ImGuiCol_ButtonActive]  = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

    // Frame BG
    colors[ImGuiCol_FrameBg]        = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
    colors[ImGuiCol_FrameBgHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
    colors[ImGuiCol_FrameBgActive]  = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

    // Tabs
    colors[ImGuiCol_Tab]                = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_TabHovered]         = ImVec4{0.38f, 0.3805f, 0.381f, 1.0f};
    colors[ImGuiCol_TabActive]          = ImVec4{0.28f, 0.2805f, 0.281f, 1.0f};
    colors[ImGuiCol_TabUnfocused]       = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};

    // Title
    colors[ImGuiCol_TitleBg]          = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_TitleBgActive]    = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
}


void ImGuiManager::init(IRender *render, IRenderPass *renderPass)
{
    YA_CORE_ASSERT(!_initialized, "ImGuiManager already initialized");

    ERenderAPI::T api = render->getAPI();

    switch (api) {
    case ERenderAPI::Vulkan:
    {
        SDL_Window *window = render->getNativeWindow<SDL_Window *>();
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

void ImGuiManager::initVulkan(SDL_Window *window, IRender *render, IRenderPass *renderPass)
{
    YA_CORE_ASSERT(!_initialized, "ImGuiManager already initialized");

    initImGuiCore();

    auto *vkRender = dynamic_cast<VulkanRender *>(render);
    YA_CORE_ASSERT(vkRender, "Render must be VulkanRender for Vulkan backend");

    auto &queue = vkRender->getGraphicsQueues()[0];

    ImGui_ImplVulkan_InitInfo initInfo{
        .ApiVersion         = vkRender->getApiVersion(),
        .Instance           = vkRender->getInstance(),
        .PhysicalDevice     = vkRender->getPhysicalDevice(),
        .Device             = vkRender->getDevice(),
        .QueueFamily        = queue.getFamilyIndex(),
        .Queue              = queue.getHandle(),
        .DescriptorPool     = nullptr,
        .DescriptorPoolSize = 64,
        .MinImageCount      = 2,
        .ImageCount         = vkRender->getSwapchainImageCount(),
        .PipelineCache      = nullptr,
        .PipelineInfoMain   = ImGui_ImplVulkan_PipelineInfo{
              .RenderPass  = renderPass->getHandleAs<VkRenderPass>(),
              .Subpass     = 0,
              .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
            // #ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
              .PipelineRenderingCreateInfo = {},
            // #endif
              .SwapChainImageUsage = 0,
        },
        .PipelineInfoForViewports = {},
        .UseDynamicRendering      = false,
        .Allocator                = vkRender->getAllocator(),
        .CheckVkResultFn          = [](VkResult err) {
            if (err != VK_SUCCESS) {
                YA_CORE_ERROR("Vulkan error in ImGui: {}", static_cast<int>(err));
            }
        },
        .MinAllocationSize          = static_cast<VkDeviceSize>(1024 * 1024),
        .CustomShaderVertCreateInfo = {},
        .CustomShaderFragCreateInfo = {},
    };

    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_Init(&initInfo);

    _initialized = true;
    YA_CORE_INFO("ImGuiManager initialized with Vulkan backend");
}

void ImGuiManager::initSDLGPU(SDL_Window *window, SDL_GPUDevice *device)
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
    ImGuizmo::BeginFrame();
    // ImGui::UpdatePlatformWindows();
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

void ImGuiManager::submitSDLGPU(SDL_GPUCommandBuffer *commandBuffer, SDL_GPURenderPass *renderpass)
{
    if (_drawData && _drawData->CmdListsCount > 0) {
        ImGui_ImplSDLGPU3_RenderDrawData(_drawData, commandBuffer, renderpass);
    }
}

EventProcessState ImGuiManager::processEvents(const SDL_Event &event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);

    auto io = &ImGui::GetIO();

    // Check mouse events
    if (io->WantCaptureMouse) {

        bool isMouseEvent = (event.type == SDL_EVENT_MOUSE_MOTION ||
                             event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                             event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                             event.type == SDL_EVENT_MOUSE_WHEEL);
        if (isMouseEvent) {
            return EventProcessState::Handled;
        }
    }
    if (io->WantCaptureKeyboard)
    {
        if (event.type == SDL_EVENT_KEY_DOWN ||
            event.type == SDL_EVENT_KEY_UP ||
            event.type == SDL_EVENT_TEXT_INPUT)
        {
            return EventProcessState::Handled;
        }
    }

    return EventProcessState::Continue;
}

EventProcessState ImGuiManager::processEvent(const Event &event)
{
    if (!bBlockEvents) {
        return EventProcessState::Continue;
    }
    auto io = &ImGui::GetIO();
    if (event.isInCategory(EEventCategory::Mouse) && io->WantCaptureMouse)
    {
        return EventProcessState::Handled;
    }
    if (event.isInCategory(EEventCategory::Keyboard) && io->WantCaptureKeyboard)
    {
        return EventProcessState::Handled;
    }
    return EventProcessState::Continue;
}

bool ImGuiManager::isWantInput() const
{
    ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

ImGuiManager &ImGuiManager::get()
{
    static ImGuiManager instance;
    return instance;
}

void *ImGuiManager::addTexture(IImageView *imageView, Sampler *sampler, EImageLayout::T layout)
{
    if (!imageView || !sampler) {
        YA_CORE_ERROR("ImGuiManager::addTexture: Invalid imageView or sampler");
        return nullptr;
    }

    // Platform-specific implementation (Vulkan for now)
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        sampler->getHandle().as<VkSampler>(),
        imageView->getHandle().as<VkImageView>(),
        toVk(layout));


    if (ds == VK_NULL_HANDLE) {
        YA_CORE_ERROR("ImGuiManager::addTexture: Failed to create descriptor set");
        return nullptr;
    }

    return static_cast<void *>(ds);
}

void ImGuiManager::removeTexture(void *textureID)
{
    if (!textureID) {
        return;
    }

    // Platform-specific implementation (Vulkan for now)
    ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(textureID));
}

bool ImGuiManager::manipulate(
    const float        *view,
    const float        *projection,
    ImGuizmo::OPERATION operation,
    ImGuizmo::MODE      mode,
    float              *matrix)
{
    return ImGuizmo::Manipulate(view, projection, operation, mode, matrix);
}

void ImGuiManager::setGizmoRect(float x, float y, float width, float height)
{
    ImGuizmo::SetRect(x, y, width, height);
}

} // namespace ya
