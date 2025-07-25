#include "App.h"
#include "Core/FName.h"
#include "Core/FileSystem/FileSystem.h"

#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"


#include "Render/Render.h"

#include <SDL3/SDL_events.h>

namespace Neon
{

// Define the static member variable
App *App::_instance = nullptr;

App *App::create()
{
    NE_CORE_ASSERT(App::_instance == nullptr, "App instance already exists!");
    App::_instance = new App();
    return App::_instance;
}

VulkanRenderPass              *pass;
std::vector<VulkanFrameBuffer> frameBuffers;
VulkanPipelineLayout          *pipelineLayout = nullptr;
VulkanPipeline                *pipeline       = nullptr;

void App::init()
{
    Logger::init();
    FileSystem::init();
    NameRegistry::init(); // Initialize FName registry

    windowProvider = new SDLWindowProvider();
    // deleteStack.push("SDLWindowProvider", windowProvider);
    windowProvider->init();

    int w, h;
    windowProvider->getWindowSize(w, h);

    _render        = new VulkanRender();
    auto *vkRender = static_cast<VulkanRender *>(_render);

    // IRender::InitParams params =
    vkRender->init(IRender::InitParams{
        .bVsync         = true,
        .renderAPI      = ERenderAPI::Vulkan,
        .windowProvider = windowProvider,
        .swapchainCI    = SwapchainCreateInfo{
               .imageFormat = EFormat::R8G8B8A8_UNORM,
               .bVsync      = true,
               .width       = static_cast<uint32_t>(w),
               .height      = static_cast<uint32_t>(h),
        },
    });

    currentRenderAPI = ERenderAPI::Vulkan;

    VkFormat surfaceFormat = vkRender->getSwapChain()->getSurfaceFormat();


    // TODO: create pipelines
    pass = new VulkanRenderPass(vkRender);
    pass->create(RenderPassCreateInfo{
        .attachments = {
            AttachmentDescription{
                .format                  = EFormat::R8G8B8A8_UNORM,
                .samples                 = ESampleCount::Sample_1,
                .loadOp                  = EAttachmentLoadOp::Clear,
                .storeOp                 = EAttachmentStoreOp::Store,
                .stencilLoadOp           = EAttachmentLoadOp::DontCare,
                .stencilStoreOp          = EAttachmentStoreOp::DontCare,
                .bInitialLayoutUndefined = true,
                .bFinalLayoutPresentSrc  = true, // for color attachments
            },
            // AttachmentDescription{
            //     .format                  = EFormat::D32_SFLOAT,
            //     .samples                 = ESampleCount::Sample_1,
            //     .loadOp                  = EAttachmentLoadOp::Clear,
            //     .storeOp                 = EAttachmentStoreOp::DontCare,
            //     .stencilLoadOp           = EAttachmentLoadOp::DontCare,
            //     .stencilStoreOp          = EAttachmentStoreOp::DontCare,
            //     .bInitialLayoutUndefined = true,
            //     .bFinalLayoutPresentSrc  = false, // for depth attachments
            // },
        },
        .subpasses = {
            RenderPassCreateInfo::SubpassInfo{
                .subpassIndex = 0,
                // .inputAttachments = {
                //     RenderPassCreateInfo::AttachmentRef{
                //         .ref    = 0, // color attachment
                //         .layout = EImageLayout::ColorAttachmentOptimal,
                //     },
                // },
                .colorAttachments = {
                    RenderPassCreateInfo::AttachmentRef{
                        .ref    = 0, // color attachment
                        .layout = EImageLayout::ColorAttachmentOptimal,
                    },
                },
            },
        },
        .dependencies = {
            RenderPassCreateInfo::SubpassDependency{
                .bSrcExternal = true,
                .srcSubpass   = 0,
                .dstSubpass   = 0,
            },
        },
    });

    VulkanSwapChain *vkSwapChain = vkRender->getSwapChain();

    const std::vector<VkImage> &images = vkSwapChain->getImages();

    // TODO: maybe copy and cause destruction?
    frameBuffers.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i)
    {
        frameBuffers[i] = VulkanFrameBuffer(vkRender, pass, vkSwapChain->getWidth(), vkSwapChain->getHeight());
        frameBuffers[i].recreate({images[i]}, vkSwapChain->getWidth(), vkSwapChain->getHeight());
    }
    pipelineLayout = new VulkanPipelineLayout(vkRender);
    pipelineLayout->create(GraphicsPipelineLayoutCreateInfo{
        .pushConstants        = {},
        .descriptorSetLayouts = {
            GraphicsPipelineLayoutCreateInfo::DescriptorSetLayout{
                .bindings = {
                    // GraphicsPipelineLayoutCreateInfo::DescriptorBinding{
                    //     .binding         = 0,
                    //     .descriptorType  = EPipelineDescriptorType::StorageBuffer,
                    //     .descriptorCount = 1,
                    //     .stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment,
                    // },
                },
            },
        },
    });
    pipeline = new VulkanPipeline(vkRender, pass, pipelineLayout);
    pipeline->recreate(GraphicsPipelineCreateInfo{
        // .pipelineLayout   = pipelineLayout,
        .shaderCreateInfo = ShaderCreateInfo{
            .shaderName        = "HelloTriangle.glsl",
            .vertexBufferDescs = {
                // VertexBufferDescription{
                //     .slot  = 0,
                //     .pitch = static_cast<uint32_t>(T2Size(EVertexAttributeFormat::Float3)),
                // },
            },
            .vertexAttributes = {
                // // (layout binding = 0, set = 0) in vec3 aPos,
                // VertexAttribute{
                //     .location   = 0,
                //     .bufferSlot = 0,
                //     .format     = EVertexAttributeFormat::Float3,
                //     .offset     = 0,
                // },
                // // (layout binding = 1, set = 0) in vec4 aColor,
                // VertexAttribute{
                //     .location   = 1,
                //     .bufferSlot = 0, // same buffer slot
                //     .format     = EVertexAttributeFormat::Float4,
                //     .offset     = static_cast<uint32_t>(T2Size(EVertexAttributeFormat::Float4)),
                // },
            },
        },
        .subPassRef         = 0,
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{},
        .multisampleState   = MultisampleState{},
        .depthStencilState  = DepthStencilState{},
        .colorBlendState    = ColorBlendState{
               .attachments = {
                ColorBlendAttachmentState{
                       .index               = 0,
                       .bBlendEnable        = true,
                       .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                       .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                       .colorBlendOp        = EBlendOp::Add,
                       .srcAlphaBlendFactor = EBlendFactor::One,
                       .dstAlphaBlendFactor = EBlendFactor::Zero,
                       .alphaBlendOp        = EBlendOp::Add,
                },
            },
        },
        .viewportState = ViewportState{},
    });
}

void Neon::App::quit()
{
    auto *vkRender = static_cast<VulkanRender *>(_render);
    vkDeviceWaitIdle(vkRender->getLogicalDevice());

    pipeline->cleanup();
    delete pipeline;
    pipelineLayout->cleanup();
    delete pipelineLayout;

    for (auto &frameBuffer : frameBuffers)
    {
        frameBuffer.clean();
    }
    delete pass;
    _render->destroy();
    windowProvider->destroy();

    delete _render;
    delete windowProvider;
}



int Neon::App::run()
{
    auto *sdlWindow = windowProvider->getNativeWindowPtr<SDL_Window>();

    while (bRunning) {

        static Uint64 lastTime    = SDL_GetTicks();
        Uint64        currentTime = SDL_GetTicks();
        float         deltaTime   = (currentTime - lastTime) / 1000.0f;
        float         fps         = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;
        lastTime                  = currentTime;


        SDL_Event evt;
        SDL_PollEvent(&evt);
        if (auto result = onEvent(evt); result != 0) {
            break;
        }
        if (auto result = iterate(deltaTime); result != 0) {
            break;
        }
    }

    return 0;
}


int Neon::App::onEvent(SDL_Event &event)
{
    inputManager.processEvent(event);


    switch (SDL_EventType(event.type))
    {
    case SDL_EVENT_FIRST:
        break;
    case SDL_EVENT_QUIT:
    {
        bRunning = false;
        return 1;
    } break;
    case SDL_EVENT_TERMINATING:
    case SDL_EVENT_LOW_MEMORY:
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
    case SDL_EVENT_DID_ENTER_FOREGROUND:
    case SDL_EVENT_LOCALE_CHANGED:
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
    case SDL_EVENT_DISPLAY_ORIENTATION:
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
    case SDL_EVENT_DISPLAY_MOVED:
    case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
    {
        // NE_CORE_INFO("window resized {}x{}",
        //              event.window.data1,
        //              event.window.data2);
        // auto vkRender = static_cast<VulkanRender *>(_render);
        // vkRender->recreateSwapChain();

    } break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    {
        bRunning = false;
        return 1;
    } break;
    case SDL_EVENT_WINDOW_HIT_TEST:
    case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
    case SDL_EVENT_WINDOW_OCCLUDED:
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
    case SDL_EVENT_WINDOW_DESTROYED:
    case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
        if (event.key.key == SDLK_ESCAPE) {
            bRunning = false;
            return 1;
        }
    } break;
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_KEYMAP_CHANGED:
    case SDL_EVENT_KEYBOARD_ADDED:
    case SDL_EVENT_KEYBOARD_REMOVED:
    case SDL_EVENT_TEXT_EDITING_CANDIDATES:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_MOUSE_ADDED:
    case SDL_EVENT_MOUSE_REMOVED:
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
    case SDL_EVENT_JOYSTICK_BALL_MOTION:
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_JOYSTICK_REMOVED:
    case SDL_EVENT_JOYSTICK_BATTERY_UPDATED:
    case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_GAMEPAD_REMAPPED:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
    case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE:
    case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED:
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_CANCELED:
    case SDL_EVENT_CLIPBOARD_UPDATE:
    case SDL_EVENT_DROP_FILE:
    case SDL_EVENT_DROP_TEXT:
    case SDL_EVENT_DROP_BEGIN:
    case SDL_EVENT_DROP_COMPLETE:
    case SDL_EVENT_DROP_POSITION:
    case SDL_EVENT_AUDIO_DEVICE_ADDED:
    case SDL_EVENT_AUDIO_DEVICE_REMOVED:
    case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:
    case SDL_EVENT_SENSOR_UPDATE:
    case SDL_EVENT_PEN_PROXIMITY_IN:
    case SDL_EVENT_PEN_PROXIMITY_OUT:
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP:
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP:
    case SDL_EVENT_PEN_MOTION:
    case SDL_EVENT_PEN_AXIS:
    case SDL_EVENT_CAMERA_DEVICE_ADDED:
    case SDL_EVENT_CAMERA_DEVICE_REMOVED:
    case SDL_EVENT_CAMERA_DEVICE_APPROVED:
    case SDL_EVENT_CAMERA_DEVICE_DENIED:
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET:
    case SDL_EVENT_RENDER_DEVICE_LOST:
    case SDL_EVENT_PRIVATE0:
    case SDL_EVENT_PRIVATE1:
    case SDL_EVENT_PRIVATE2:
    case SDL_EVENT_PRIVATE3:
    case SDL_EVENT_POLL_SENTINEL:
    case SDL_EVENT_USER:
    case SDL_EVENT_LAST:
    case SDL_EVENT_ENUM_PADDING:
        break;
    }
    return 0;
};


int Neon::App::iterate(float dt)
{
    inputManager.update();


#if USE_VULKAN
    auto render = static_cast<VulkanRender *>(_render);
    // render->DrawFrame(); // Draw triangle!
    // render->OnPostUpdate();

#endif
    // Handle input, update logic, render, etc.
    // This is where the main loop logic would go.
    return 0; // Continue running
}

void App::drawTriangle()
{
    //     // Wait for previous frame
    //     // vkWaitForFences(m_LogicalDevice, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
    //     // vkResetFences(m_LogicalDevice, 1, &m_inFlightFence);

    //     auto vkRender = static_cast<VulkanRender *>(_render);
    //     // Acquire next image
    //     uint32_t imageIndex;
    //     // VkResult result = vkRender->getSwapChain().acquireNextImage(imageIndex, m_imageAvailableSemaphore);

    //     // if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    //     //     recreateSwapChain();
    //     //     return;
    //     // }
    //     // else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    //     //     NE_CORE_ASSERT(false, "Failed to acquire swap chain image!");
    //     // }

    //     // Update uniform buffer
    //     // updateUniformBuffer();

    //     // Reset and begin command buffer
    //     // VkCommandBuffer commandBuffer = m_commandBuffers[imageIndex];
    //     // vkResetCommandBuffer(commandBuffer, 0);

    //     // VkCommandBufferBeginInfo beginInfo{};
    //     // beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    //     // if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    //     //     NE_CORE_ASSERT(false, "Failed to begin recording command buffer!");
    //     // }


    //     // Begin render pass
    //     VkRenderPassBeginInfo renderPassInfo{};
    //     renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    //     // renderPassInfo.renderPass        = m_renderPass.getRenderPass();
    //     // renderPassInfo.framebuffer       = m_renderPass.getFramebuffers()[imageIndex];
    //     renderPassInfo.renderArea.offset = {0, 0};
    //     renderPassInfo.renderArea.extent = vkRender->getSwapChain().getExtent();

    //     std::array<VkClearValue, 2> clearValues{};
    //     clearValues[0].color        = {{1.0f, 0.0f, 1.0f, 1.0f}}; // Clear color: black
    //     clearValues[1].depthStencil = {1.0f, 0};                  // Clear depth to 1.0

    //     renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    //     renderPassInfo.pClearValues    = clearValues.data();

    //     vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    //     // Bind graphics pipeline - use the first available pipeline or a specific one
    //     FName pipelineName = "DefaultTriangle"; // Default name
    //     if (!m_pipelineManager.hasPipeline(pipelineName)) {
    //         // Get the first available pipeline
    //         auto pipelineNames = m_pipelineManager.getPipelineNames();
    //         if (!pipelineNames.empty()) {
    //             pipelineName = pipelineNames[0];
    //         }
    //         else {
    //             NE_CORE_ERROR("No pipelines available for rendering!");
    //             return;
    //         }
    //     }

    //     if (!m_pipelineManager.bindPipeline(commandBuffer, pipelineName)) {
    //         NE_CORE_ERROR("Failed to bind pipeline: {}", pipelineName);
    //         return;
    //     }

    //     // Bind descriptor sets for the active pipeline
    //     m_pipelineManager.bindDescriptorSets(commandBuffer, pipelineName);

    // #if 1 // need to set the vulkan dynamic state, see VkPipelineDynamicStateCreateInfo
    //     // Set viewport
    //     VkViewport viewport{};
    //     viewport.x        = 0.0f;
    //     viewport.y        = 0.0f;
    //     viewport.width    = static_cast<float>(m_swapChain.getExtent().width);
    //     viewport.height   = static_cast<float>(m_swapChain.getExtent().height);
    //     viewport.minDepth = 0.0f;
    //     viewport.maxDepth = 1.0f;
    //     vkCmdSetViewport(commandBuffer, 0, 1, &viewport);


    //     // Set scissor
    //     VkRect2D scissor{};
    //     scissor.offset = {0, 0};
    //     scissor.extent = m_swapChain.getExtent();
    //     vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    // #endif

    //     // Bind vertex buffer
    //     VkBuffer     vertexBuffers[] = {m_vertexBuffer};
    //     VkDeviceSize offsets[]       = {0};
    //     vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    //     // Draw triangle
    //     vkCmdDraw(commandBuffer, static_cast<uint32_t>(m_triangleVertices.size()), 1, 0, 0);


    // #if 1 // multiple renderpass
    //     // vkCmdNextSubpass2()
    //     // vkCmdNextSubpass()
    // #endif

    //     // End render pass and command buffer
    //     vkCmdEndRenderPass(commandBuffer);

    //     if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    //         NE_CORE_ASSERT(false, "Failed to record command buffer!");
    //     }

    //     // Submit command buffer
    //     VkSemaphore          waitSemaphores[]   = {m_imageAvailableSemaphore};
    //     VkPipelineStageFlags waitStages[]       = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    //     VkSemaphore          signalSemaphores[] = {m_renderFinishedSemaphore};


    //     VkSubmitInfo submitInfo{
    //         .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    //         .waitSemaphoreCount   = 1,
    //         .pWaitSemaphores      = waitSemaphores,
    //         .pWaitDstStageMask    = waitStages,
    //         .commandBufferCount   = 1,
    //         .pCommandBuffers      = &commandBuffer,
    //         .signalSemaphoreCount = 1,
    //         .pSignalSemaphores    = signalSemaphores,
    //     };


    //     if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_inFlightFence) != VK_SUCCESS) {
    //         NE_CORE_ASSERT(false, "Failed to submit draw command buffer!");
    //     }

    //     // Present
    //     result = m_swapChain.presentImage(imageIndex, m_renderFinishedSemaphore, m_PresentQueue);

    //     if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    //         recreateSwapChain();
    //     }
    //     else if (result != VK_SUCCESS) {
    //         NE_CORE_ASSERT(false, "Failed to present swap chain image!");
    //     }
}

} // namespace Neon