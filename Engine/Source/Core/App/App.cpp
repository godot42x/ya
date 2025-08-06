#include "App.h"
#include "Core/FName.h"
#include "Core/FileSystem/FileSystem.h"

#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"


#include "Render/Render.h"

#include <SDL3/SDL.h>
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

VulkanRenderPass              *renderpass;
std::vector<VulkanFrameBuffer> frameBuffers;
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
    auto *vkRender = dynamic_cast<VulkanRender *>(_render);

    currentRenderAPI = ERenderAPI::Vulkan;

    /**
      In vulkan:
      1. Create instance
      2. Create surface
      3. Find physical device
      4. Create logical device
      5. Create swap chain
      6. Other device resources: command pool{command buffers}, fences, semaphores, etc.
     */
    vkRender->init(IRender::InitParams{
        .renderAPI      = ERenderAPI::Vulkan,
        .bVsync         = true,
        .windowProvider = windowProvider,
        .swapchainCI    = SwapchainCreateInfo{
               .imageFormat = EFormat::R8G8B8A8_UNORM,
               .bVsync      = true,
               .width       = static_cast<uint32_t>(w),
               .height      = static_cast<uint32_t>(h),
        },
    });


    VkFormat surfaceFormat = vkRender->getSwapChain()->getSurfaceFormat();


    /**
      In Vulkan:
        Create render pass and subpass
        define all attachments,
        input/color/depth/resolved attachment ref from all attachments
        and each subpasses dependencies (source -> next)
     */
    renderpass = new VulkanRenderPass(vkRender);
    renderpass->create(RenderPassCreateInfo{
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
    // TOD: this should be a resources part of logical device or swapchain. And should be recreated when swapchain is recreated
    frameBuffers.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i)
    {
        frameBuffers[i] = VulkanFrameBuffer(vkRender, renderpass, vkSwapChain->getWidth(), vkSwapChain->getHeight());
        frameBuffers[i].recreate({images[i]}, vkSwapChain->getWidth(), vkSwapChain->getHeight());
    }

    /**
     In Vulkan:
        define pipeline layout
        the layout(set=?, binding=?) for each descriptor set:
        - uniform buffer(camera, light)
        - push constants?
        - texture samplers?
    */
    defaultPipelineLayout = new VulkanPipelineLayout(vkRender);
    defaultPipelineLayout->create(GraphicsPipelineLayoutCreateInfo{
        .pushConstants        = {},
        .descriptorSetLayouts = {
            GraphicsPipelineLayoutCreateInfo::DescriptorSetLayout{
                // .bindings = {
                // GraphicsPipelineLayoutCreateInfo::DescriptorBinding{
                //     .binding         = 0,
                //     .descriptorType  = EPipelineDescriptorType::StorageBuffer,
                //     .descriptorCount = 1,
                //     .stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment,
                // },
                // },
            },
        },
    });

    /**
      In Vulkan:
        define the pipelines
        1. Shader Part
            -  shader programs from vert/frag/geom/comp shaders source codes
            -  the vertex layout define/from reflection, like glVertexAttribPointer()
        2. reference to the pipeline layout(seems one layout can be compatible with different pipelines)
        3. reference to the subpass
        4. other states initial settings (unlike OpenGL, all state are immutable in Vulkan by default):
            -  rasterization state
            -  multisample state
            -  depth/stencil state
            -  color blend state
            -  viewport state
    */
    pipeline = new VulkanPipeline(vkRender, renderpass, defaultPipelineLayout);
    pipeline->recreate(GraphicsPipelineCreateInfo{
        // .pipelineLayout   = pipelineLayout,
        .shaderCreateInfo = ShaderCreateInfo{
            .shaderName        = "HelloTriangle.glsl",
            .bDeriveFromShader = false,
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
        .subPassRef = 0,
        // define what state need to dynamically modified in render pass execution
        .dynamicFeatures    = {},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .frontFace   = EFrontFaceType::CounterClockWise, // GL
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{},
        .colorBlendState   = ColorBlendState{
              .attachments = {
                ColorBlendAttachmentState{
                      .index               = 0,
                      .bBlendEnable        = false,
                      .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                      .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                      .colorBlendOp        = EBlendOp::Add,
                      .srcAlphaBlendFactor = EBlendFactor::One,
                      .dstAlphaBlendFactor = EBlendFactor::Zero,
                      .alphaBlendOp        = EBlendOp::Add,
                      .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                },
            },
        },
        .viewportState = ViewportState{},
    });

    _firstGraphicsQueue = &vkRender->getGraphicsQueues()[0];
    _firstPresentQueue  = &vkRender->getPresentQueues()[0];
}

void Neon::App::quit()
{
    auto *vkRender = static_cast<VulkanRender *>(_render);
    vkDeviceWaitIdle(vkRender->getLogicalDevice());

    pipeline->cleanup();
    delete pipeline;
    defaultPipelineLayout->cleanup();
    delete defaultPipelineLayout;

    for (auto &frameBuffer : frameBuffers)
    {
        frameBuffer.clean();
    }
    delete renderpass;
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
    auto render = dynamic_cast<VulkanRender *>(_render);
    drawTriangle();
#endif
    // Handle input, update logic, render, etc.
    // This is where the main loop logic would go.
    return 0; // Continue running
}

void App::drawTriangle()
{

    auto vkRender = static_cast<VulkanRender *>(_render);
    vkDeviceWaitIdle(vkRender->getLogicalDevice());

    // SDL_Delay(1000 / 30); // Simulate frame time, remove in production


    // 0: optional:use fence/semaphore to synchronize with previous frame
    // Wait for previous frame
    // vkWaitForFences(m_LogicalDevice, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
    // vkResetFences(m_LogicalDevice, 1, &m_inFlightFence);

    // 1. acquire swapchain image
    uint32_t imageIndex = -1;
    vkRender->getSwapChain()->acquireNextImage(
        vkRender->getImageAvailableSemaphore(), // wait for image available semaphore
        nullptr,
        imageIndex);


    // 2. begin command buffer
    VkCommandBuffer commandBuffer = vkRender->getCommandBuffers()[imageIndex];
    // vkResetCommandBuffer(commandBuffer, 0);
    begin(commandBuffer);

    // 3 begin render pass && bind frame buffer
    // TODO: subpasses?
    std::vector<VkClearValue> clearValues{};
    clearValues.resize(1);
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // Clear color: black
    // clearValues[1].depthStencil = {1.0f, 0};                  // Clear depth to 1.0

    renderpass->begin(commandBuffer,
                      frameBuffers[imageIndex].getHandle(),
                      vkRender->getSwapChain()->getExtent(),
                      {clearValues.begin(), clearValues.end()});


    // 4. bind descriptor sets,
    // vkCmdBindDescriptorSets(commandBuffer,
    //                         VK_PIPELINE_BIND_POINT_GRAPHICS,
    //                         pipelineLayout->getHandle(),
    //                         0,        // first set
    //                         0,        // descriptor set count
    //                         nullptr,  // descriptor sets
    //                         0,        // dynamic offset count
    //                         nullptr); // dynamic offsets

    // 5. bind pipeline
    pipeline->bind(commandBuffer);



    // 6. setup vertex buffer, index buffer (2d payloads? upload?)
    // 7. other resources, uniform buffers, etc.


#if 1 // Dynamic state
    // need set VkPipelineDynamicStateCreateInfo
    // or those properties should be modified in the pipeline recreation if needed.
    // but sometimes that we want to enable depth or color-blend state dynamically

    // Set viewport
    // VkViewport viewport{};
    // viewport.x        = 0.0f;
    // viewport.y        = 0.0f;
    // viewport.width    = static_cast<float>(m_swapChain.getExtent().width);
    // viewport.height   = static_cast<float>(m_swapChain.getExtent().height);
    // viewport.minDepth = 0.0f;
    // viewport.maxDepth = 1.0f;
    // vkCmdSetViewport(commandBuffer, 0, 1, &viewport);


    // Set scissor
    // VkRect2D scissor{};
    // scissor.offset = {0, 0};
    // scissor.extent = m_swapChain.getExtent();
    // vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
#endif

    //     // Bind vertex buffer
    //     VkBuffer     vertexBuffers[] = {m_vertexBuffer};
    //     VkDeviceSize offsets[]       = {0};
    //     vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    // 8. draw triangle
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);



    // 9. end render pass
    // TODO: subpasses?
    // #if 1 // multiple renderpass
    //     // vkCmdNextSubpass2()
    //     // vkCmdNextSubpass()
    // #endif
    renderpass->end(commandBuffer);


    // 10. end command buffer
    end(commandBuffer);

    // 11. submit command buffer
    //     VkSemaphore          waitSemaphores[]   = {m_imageAvailableSemaphore};
    //     VkPipelineStageFlags waitStages[]       = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    //     VkSemaphore          signalSemaphores[] = {m_renderFinishedSemaphore};
    // _firstGraphicsQueue.waitIdle();
    _firstGraphicsQueue->submit(
        {
            commandBuffer,
        },
        // this tell next submit, wait last render
        {
            vkRender->getImageAvailableSemaphore(),
        },
        // this semaphore will be emit after submit complete/ render finished?
        {
            vkRender->getRenderFinishedSemaphore(),

        });
    _firstPresentQueue->waitIdle();

    // 12. present swapchain image
    VkResult result = vkRender->getSwapChain()->presentImage(
        imageIndex,
        _firstGraphicsQueue->getHandle(),
        // and then do present image after render finished?
        {
            vkRender->getRenderFinishedSemaphore(),
        });
}

} // namespace Neon