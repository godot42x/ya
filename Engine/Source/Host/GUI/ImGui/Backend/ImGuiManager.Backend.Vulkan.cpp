#include "Host/GUI/ImGui/ImGuiSystem.h"

#include "RHI/Backend/Vulkan/VulkanRender.h"
#include "RHI/Backend/Vulkan/VulkanUtils.h"

#include <vulkan/vulkan.h>

#define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
#include <imgui_impl_vulkan.h>

namespace ya
{

namespace
{

constexpr uint32_t IMGUI_DESCRIPTOR_POOL_SIZE = 512;

} // namespace

void ImGuiManager::initVulkan(IWindowProvider* window, IRender* render, IRenderPass* renderPass)
{
    YA_CORE_ASSERT(!_initialized, "ImGuiManager already initialized");

    initImGuiCore();

    auto* vkRender = dynamic_cast<VulkanRender*>(render);
    YA_CORE_ASSERT(vkRender, "Render must be VulkanRender for Vulkan backend");

    auto& queue = vkRender->getGraphicsQueues()[0];

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
        .DescriptorPoolSize = IMGUI_DESCRIPTOR_POOL_SIZE,
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
        .CheckVkResultFn          = [](VkResult err)
        {
            if (err != VK_SUCCESS) {
                YA_CORE_ERROR("Vulkan error in ImGui: {} -> {}", static_cast<int>(err), std::to_string(err));
            }
        },
        .MinAllocationSize          = static_cast<VkDeviceSize>(1024 * 1024),
        .CustomShaderVertCreateInfo = {},
        .CustomShaderFragCreateInfo = {},
    };

    ImGui_ImplSDL3_InitForVulkan(static_cast<SDL_Window*>(window->getNativeWindowHandle()));
    ImGui_ImplVulkan_Init(&initInfo);

    _initialized = true;
    YA_CORE_INFO("ImGuiManager initialized with Vulkan backend");
}

#if IMGUI_SDL3_GPU
void ImGuiManager::initSDLGPU(IWindowProvider* window, SDL_GPUDevice* device)
{
    YA_CORE_ASSERT(!_initialized, "ImGuiManager already initialized");

    initImGuiCore();

    auto* nativeWindow = static_cast<SDL_Window*>(window->getNativeWindowHandle());
    ImGui_ImplSDL3_InitForSDLGPU(nativeWindow);
    SDL_WaitForGPUSwapchain(device, nativeWindow);
    auto swapChainFormat = SDL_GetGPUSwapchainTextureFormat(device, nativeWindow);
    YA_CORE_DEBUG("Swapchain format: {}, device: {}, window: {}",
                  static_cast<int>(swapChainFormat),
                  (uintptr_t)device,
                  (uintptr_t)window);
    if (swapChainFormat == SDL_GPU_TEXTUREFORMAT_INVALID) {
        YA_CORE_ERROR("Failed to get swapchain texture format: {}", SDL_GetError());
    }

    _initialized = true;
    YA_CORE_INFO("ImGuiManager initialized with SDLGPU backend");
}
#endif

void ImGuiManager::submitVulkan(VkCommandBuffer cmdBuf, VkPipeline pipeline)
{
    if (_drawData && _drawData->CmdListsCount > 0) {
        ImGui_ImplVulkan_RenderDrawData(_drawData, cmdBuf, pipeline);
    }
}

#if IMGUI_SDL3_GPU
void ImGuiManager::submitSDLGPU(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderpass)
{
    if (_drawData && _drawData->CmdListsCount > 0) {
        ImGui_ImplSDLGPU3_RenderDrawData(_drawData, commandBuffer, renderpass);
    }
}
#endif

} // namespace ya
