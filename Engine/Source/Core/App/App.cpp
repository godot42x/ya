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
VulkanPipeline                *pipeline = nullptr;


// 每帧需要独立的同步对象
// 这是Flight Frames的关键：每个飞行中的帧都有自己的同步原语
std::vector<VkSemaphore> imageAvailableSemaphores;  // 每帧的图像可用信号量
std::vector<VkSemaphore> submittedSignalSemaphores; // 每帧的渲染完成信号量
std::vector<VkFence>     frameFences;               // 每帧的CPU-GPU同步栅栏

// 手动设置多层cpu缓冲，让 cpu 在多个帧之间轮转, cpu and gpu can work in parallel
// but frame count should be limited and considered  with performance
static uint32_t currentFrame = 0;

void App::init()
{
    Logger::init();
    FileSystem::init();
    NameRegistry::init(); // Initialize FName registry

    windowProvider = new SDLWindowProvider();
    // deleteStack.push("SDLWindowProvider", windowProvider);
    windowProvider->init();

    int w = 0, h = 0;
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
               .imageFormat   = EFormat::R8G8B8A8_UNORM,
               .bVsync        = true,
               .minImageCount = 3,
               .width         = static_cast<uint32_t>(w),
               .height        = static_cast<uint32_t>(h),
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

    submissionResourceSize = static_cast<int>(images.size());

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
        // .dynamicFeatures = EPipelineDynamicFeature::DepthTest | EPipelineDynamicFeature::AlphaBlend,
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

    initSemaphoreAndFence();
}

void Neon::App::quit()
{
    auto *vkRender = static_cast<VulkanRender *>(_render);
    vkDeviceWaitIdle(vkRender->getLogicalDevice());

    releaseSemaphoreAndFence();

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

    onDraw();
    return 0;
}

void App::onDraw()
{
    auto vkRender = static_cast<VulkanRender *>(_render);
    // vkDeviceWaitIdle(vkRender->getLogicalDevice());
    // SDL_Delay(1000 / 30); // Simulate frame time, remove in production


    // ✅ Flight Frames关键步骤1: CPU等待当前帧对应的fence
    // 这确保CPU不会在GPU还在使用资源时就开始修改它们
    // 例如：如果MAX_FRAMES_IN_FLIGHT=2，当渲染第3帧时，等待第1帧完成
    VK_CALL(vkWaitForFences(vkRender->getLogicalDevice(),
                            1,
                            &frameFences[currentFrame],
                            VK_TRUE,
                            UINT64_MAX));

    // 重置fence为未信号状态，准备给GPU在本帧结束时发送信号
    VK_CALL(vkResetFences(vkRender->getLogicalDevice(),
                          1,
                          &frameFences[currentFrame]));


    // 目前启用fence，否则 semaphore 会被不同的帧重用. TODO:  Use a separate semaphore per swapchain image. Index these semaphores using the index of the acquired image.
    uint32_t imageIndex = -1;
    vkRender->getSwapChain()->acquireNextImage(
        imageAvailableSemaphores[currentFrame], // 当前帧的图像可用信号量
        frameFences[currentFrame],
        imageIndex);


    // 2. begin command buffer
    VkCommandBuffer curCmdBuf = vkRender->getCommandBuffers()[imageIndex];
    vkResetCommandBuffer(curCmdBuf, 0); // Reset command buffer before recording
    begin(curCmdBuf);

    // 3 begin render pass && bind frame buffer
    // TODO: subpasses?
    std::vector<VkClearValue> clearValues{};
    clearValues.resize(1);
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // Clear color: black
    // clearValues[1].depthStencil = {1.0f, 0};                  // Clear depth to 1.0

    renderpass->begin(curCmdBuf,
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
    pipeline->bind(curCmdBuf);



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
    vkCmdDraw(curCmdBuf, 3, 1, 0, 0);



    // 9. end render pass
    // TODO: subpasses?
    // #if 1 // multiple renderpass
    //     // vkCmdNextSubpass2()
    //     // vkCmdNextSubpass()
    // #endif
    renderpass->end(curCmdBuf);


    // 10. end command buffer
    end(curCmdBuf);

    // 11. submit command buffer
    // _firstGraphicsQueue.waitIdle();
    // auto curQue = currentFrame == 0 ? _firstGraphicsQueue : _firstPresentQueue;
    _firstGraphicsQueue->submit(
        {
            curCmdBuf,
        },
        // 等待条件：当前帧的图像可用信号量被触发后才开始渲染
        {
            imageAvailableSemaphores[currentFrame],
        },
        // 完成信号：渲染完成后触发当前帧的提交完成信号量
        {
            submittedSignalSemaphores[currentFrame],
        },
        frameFences[currentFrame] // GPU完成所有工作后会发送此fence信号
    );


    VkResult result = vkRender->getSwapChain()->presentImage(
        imageIndex,
        _firstPresentQueue->getHandle(),
        // 等待条件：当前帧的渲染完成信号量被触发后才呈现
        {
            submittedSignalSemaphores[currentFrame],
        });

    if (result != VK_SUCCESS) {
        NE_CORE_WARN("Failed to present swap chain image! Result: {}", result);
    }

    // ✅ Flight Frames关键步骤5: 切换到下一个飞行帧
    // 使用模运算实现环形缓冲区，在多个帧之间循环
    // 例如：0 -> 1 -> 0 -> 1 ... (当submissionResourceSize=2时)
    currentFrame = (currentFrame + 1) % submissionResourceSize;
}

#pragma region synchronization resources

void App::initSemaphoreAndFence()
{
    /* ✅ Flight Frames机制的同步对象初始化

       SDL与Vulkan没有像OpenGL那样的"swap buffers"概念（如SDL_GL_SwapWindow()）。
       在Vulkan中，呈现机制通过Vulkan的swapchain扩展来管理，
       因此我们使用semaphore和fence来同步图像获取和渲染完成。

       Flight Frames需要为每个并行帧创建独立的同步对象：
       - imageAvailableSemaphores: GPU-GPU同步，swapchain图像可用信号
       - submittedSignalSemaphores: GPU-GPU同步，渲染完成信号
       - frameFences: CPU-GPU同步，CPU等待GPU完成帧渲染
    */

    auto vkRender = static_cast<VulkanRender *>(_render);

    VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0, // No special flags
    };

    VkResult ret = {};
    // NOTICE : 应该创建与swapchain图像数量相同的信号量数量
    // 以避免不同图像之间的资源竞争（如信号量) VUID-vkQueueSubmit-pSignalSemaphores-00067

    // ✅ 为每个飞行帧分配独立的同步对象数组
    // 这是Flight Frames的核心：避免不同帧之间的资源竞争
    imageAvailableSemaphores.resize(submissionResourceSize);
    submittedSignalSemaphores.resize(submissionResourceSize);
    frameFences.resize(submissionResourceSize);

    // ✅ 循环创建每个飞行帧的同步对象
    for (uint32_t i = 0; i < submissionResourceSize; i++) {
        // 创建图像可用信号量：当swapchain图像准备好被渲染时发出信号
        ret = vkCreateSemaphore(vkRender->getLogicalDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
        NE_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create image available semaphore! Result: {}", ret);

        // 创建渲染完成信号量：当GPU完成渲染命令时发出信号
        ret = vkCreateSemaphore(vkRender->getLogicalDevice(), &semaphoreInfo, nullptr, &submittedSignalSemaphores[i]);
        NE_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create render finished semaphore! Result: {}", ret);

        // 创建帧fence：用于CPU等待GPU完成整个帧的处理
        // ⚠️ 重要：初始状态设为已信号(SIGNALED)，这样第一帧不会被阻塞
        VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT // 开始时就处于已信号状态
        };

        ret = vkCreateFence(vkRender->getLogicalDevice(), &fenceInfo, nullptr, &frameFences[i]);
        NE_CORE_ASSERT(ret == VK_SUCCESS, "failed to create fence!");

        vkRender->setDebugObjectName(VK_OBJECT_TYPE_SEMAPHORE,
                                     (uintptr_t)imageAvailableSemaphores[i],
                                     std::format("ImageAvailableSemaphore_{}", i).c_str());
        vkRender->setDebugObjectName(VK_OBJECT_TYPE_SEMAPHORE,
                                     (uintptr_t)submittedSignalSemaphores[i],
                                     std::format("SubmittedSignalSemaphore_{}", i).c_str());
        vkRender->setDebugObjectName(VK_OBJECT_TYPE_FENCE,
                                     (uintptr_t)frameFences[i],
                                     std::format("FrameFence_{}", i).c_str());
    }
}

void App::releaseSemaphoreAndFence()
{
    auto vkRender = static_cast<VulkanRender *>(_render);
    vkDeviceWaitIdle(vkRender->getLogicalDevice());
    for (uint32_t i = 0; i < submissionResourceSize; i++) {
        vkDestroySemaphore(vkRender->getLogicalDevice(), imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(vkRender->getLogicalDevice(), submittedSignalSemaphores[i], nullptr);
        vkDestroyFence(vkRender->getLogicalDevice(), frameFences[i], nullptr);
    }
}

#pragma endregion


} // namespace Neon
