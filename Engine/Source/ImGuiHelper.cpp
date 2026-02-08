#include "ImGuiHelper.h"
#include "Render/Core/Image.h"

#include <unordered_map>
#include <vulkan/vulkan.h>


#define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
#include <imgui_freetype.h>
#include <imgui_impl_vulkan.h>


#include "Core/Log.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Platform/Render/Vulkan/VulkanUtils.h"



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

    // Load main font (JetBrains Mono)
    ImFont *mainFont = nullptr;
    {
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 2;

        mainFont = io->Fonts->AddFontFromFileTTF(
            "Engine/Content/Fonts/JetBrainsMono-Medium.ttf",
            16.0f,
            &fontConfig);
    }
    if (!mainFont)
    {
        YA_CORE_ERROR("Failed to load main font");
        mainFont = io->Fonts->AddFontDefault();
    }

    static ImFontConfig cfg;
    static ImWchar      ranges[] = {0x1, 0x1FFFF, 0};

    cfg.MergeMode = true;
    cfg.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_LoadColor;
    io->Fonts->AddFontFromFileTTF("Engine/Content/Fonts/seguiemj.ttf", 16.0f, &cfg /*, ranges*/);



    // io->Fonts->AddFontDefault();


    ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());

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

    const bool                    useDynamicRendering = (renderPass == nullptr);
    VkFormat                      swapchainFormat     = VK_FORMAT_UNDEFINED;
    VkPipelineRenderingCreateInfo pipelineRenderingCI{};
    if (useDynamicRendering) {
        swapchainFormat     = toVk(vkRender->getSwapchain()->getFormat());
        pipelineRenderingCI = VkPipelineRenderingCreateInfo{
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext                   = nullptr,
            .viewMask                = 0,
            .colorAttachmentCount    = 1,
            .pColorAttachmentFormats = &swapchainFormat,
            .depthAttachmentFormat   = VK_FORMAT_UNDEFINED,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
        };
    }

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
              .RenderPass                  = useDynamicRendering ? VK_NULL_HANDLE : renderPass->getHandleAs<VkRenderPass>(),
              .Subpass                     = 0,
              .MSAASamples                 = VK_SAMPLE_COUNT_1_BIT,
              .PipelineRenderingCreateInfo = pipelineRenderingCI,
              .SwapChainImageUsage         = 0,
        },
        .PipelineInfoForViewports = {},
        .UseDynamicRendering      = useDynamicRendering,
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

#if IMGUI_SDL3_GPU
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
#endif

void ImGuiManager::shutdown()
{
    if (!_initialized) {
        return;
    }

    ImGuiHelper::ClearImageCache();
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();

    _initialized = false;
    YA_CORE_INFO("ImGuiManager shutdown");
}

void ImGuiManager::beginFrame()
{
    YA_PROFILE_FUNCTION()
    YA_CORE_ASSERT(_initialized, "ImGuiManager not initialized");

    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    // ImGui::UpdatePlatformWindows();
}

void ImGuiManager::endFrame()
{
    YA_PROFILE_FUNCTION()
    ImGui::EndFrame();
}

bool ImGuiManager::render()
{
    YA_PROFILE_FUNCTION()
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

#if IMGUI_SDL3_GPU
void ImGuiManager::submitSDLGPU(SDL_GPUCommandBuffer *commandBuffer, SDL_GPURenderPass *renderpass)
{
    if (_drawData && _drawData->CmdListsCount > 0) {
        ImGui_ImplSDLGPU3_RenderDrawData(_drawData, commandBuffer, renderpass);
    }
}
#endif

EventProcessState ImGuiManager::processEvents(const SDL_Event &event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);

    auto io = &ImGui::GetIO();

    // Check mouse events
    if (io->WantCaptureMouse) {

        if (ImGuizmo::IsOver() && !ImGuizmo::IsUsingAny()) {
        }
        else {
            return EventProcessState::Handled;
        }
    }
    if (io->WantCaptureKeyboard)
    {
        if (event.type == SDL_EVENT_KEY_DOWN ||
            event.type == SDL_EVENT_KEY_UP ||
            event.type == SDL_EVENT_TEXT_INPUT)
        {
            if (ImGuizmo::IsOver() && !ImGuizmo::IsUsingAny()) {
            }
            else {
                return EventProcessState::Handled;
            }
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

namespace
{
struct ImageCacheKey
{
    IImageView *imageView = nullptr;
    Sampler    *sampler   = nullptr;

    bool operator==(const ImageCacheKey &other) const
    {
        return imageView == other.imageView && sampler == other.sampler;
    }
};

struct ImageCacheKeyHash
{
    size_t operator()(const ImageCacheKey &key) const noexcept
    {
        size_t h1 = std::hash<void *>{}(key.imageView);
        size_t h2 = std::hash<void *>{}(key.sampler);
        return h1 ^ (h2 << 1);
    }
};

struct ImageCacheEntry
{
    ImageViewHandle handle = {};
    void           *ds     = nullptr;
};

std::unordered_map<ImageCacheKey, ImageCacheEntry, ImageCacheKeyHash> g_imageCache;

void *getOrCreateDescriptorSet(IImageView *imageView, Sampler *sampler)
{
    if (!imageView || !sampler) {
        return nullptr;
    }

    ImageCacheKey   key{.imageView = imageView, .sampler = sampler};
    ImageViewHandle handle = imageView->getHandle();

    auto it = g_imageCache.find(key);
    if (it != g_imageCache.end()) {
        if (it->second.ds && it->second.handle == handle) {
            return it->second.ds;
        }
        if (it->second.ds) {
            ImGuiManager::removeTexture(it->second.ds);
            YA_CORE_TRACE("Invalidated ImGui descriptor set for imageView: {}, sampler: {}",
                          handle.ptr,
                          sampler->getHandle().ptr);
        }
    }

    void *ds = ImGuiManager::addTexture(imageView, sampler, EImageLayout::ShaderReadOnlyOptimal);
    if (!ds) {
        return nullptr;
    }

    g_imageCache[key] = ImageCacheEntry{handle, ds};
    return ds;
}
} // namespace

namespace ImGuiHelper
{
bool Image(IImageView        *imageView,
           Sampler           *sampler,
           const std::string &alt,
           const ImVec2      &size,
           const ImVec2      &uv0,
           const ImVec2      &uv1,
           const ImVec4      &tint,
           const ImVec4      &border)
{
    void *ds = getOrCreateDescriptorSet(imageView, sampler);
    if (ds) {
        ImGui::Image(ds, size, uv0, uv1, tint, border);
        return true;
    }
    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Invalid Image: %s", alt.c_str());
    return false;
}

void ClearImageCache()
{
    for (auto &entry : g_imageCache) {
        if (entry.second.ds) {
            ImGuiManager::removeTexture(entry.second.ds);
        }
    }
    g_imageCache.clear();
}
} // namespace ImGuiHelper

void ImGuiManager::setGizmoRect(float x, float y, float width, float height)
{
    ImGuizmo::SetRect(x, y, width, height);
}


using namespace ImGui;
static void metricsHelpMarker(const char *desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void ImGuiManager::onRenderGUI()
{
    auto &io    = ImGui::GetIO();
    auto &style = ImGui::GetStyle();


    ShowFontSelector("Fonts##Selector");
    if (DragFloat("FontSizeBase", &style.FontSizeBase, 0.20f, 5.0f, 100.0f, "%.0f"))
        style._NextFrameFontSizeBase = style.FontSizeBase; // FIXME: Temporary hack until we finish remaining work.
    SameLine(0.0f, 0.0f);
    Text(" (out %.2f)", GetFontSize());
    DragFloat("FontScaleMain", &style.FontScaleMain, 0.02f, 0.5f, 4.0f);
    // BeginDisabled(GetIO().ConfigDpiScaleFonts);
    DragFloat("FontScaleDpi", &style.FontScaleDpi, 0.02f, 0.5f, 4.0f);
    // SetItemTooltip("When io.ConfigDpiScaleFonts is set, this value is automatically overwritten.");
    // EndDisabled();

    // Simplified Settings (expose floating-pointer border sizes as boolean representing 0.0f or 1.0f)
    if (SliderFloat("FrameRounding", &style.FrameRounding, 0.0f, 12.0f, "%.0f"))
        style.GrabRounding = style.FrameRounding; // Make GrabRounding always the same value as FrameRounding
    {
        bool border = (style.WindowBorderSize > 0.0f);
        if (Checkbox("WindowBorder", &border)) {
            style.WindowBorderSize = border ? 1.0f : 0.0f;
        }
    }
    SameLine();
    {
        bool border = (style.FrameBorderSize > 0.0f);
        if (Checkbox("FrameBorder", &border)) {
            style.FrameBorderSize = border ? 1.0f : 0.0f;
        }
    }
    SameLine();
    {
        bool border = (style.PopupBorderSize > 0.0f);
        if (Checkbox("PopupBorder", &border)) {
            style.PopupBorderSize = border ? 1.0f : 0.0f;
        }
    }
}

} // namespace ya
