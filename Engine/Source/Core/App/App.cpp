#include "App.h"
#include "Core/EditorCamera.h"
#include "Core/FName.h"
#include "Core/FileSystem/FileSystem.h"

#include "Math/Geometry.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"



#include "Render/Render.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>

#include "glm/glm.hpp"



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

// std::vector<std::shared_ptr<VulkanImage>> depthImages;
const auto DEPTH_FORMAT = EFormat::D32_SFLOAT;

// 手动设置多层cpu缓冲，让 cpu 在多个帧之间轮转, cpu and gpu can work in parallel
// but frame count should be limited and considered  with performance
static uint32_t currentFrame = 0;

std::shared_ptr<VulkanBuffer> vertexBuffer;
std::shared_ptr<VulkanBuffer> indexBuffer;
uint32_t                      indexSize = -1;

glm::mat4 matModel;

EditorCamera   camera;
vk::ImguiState imgui;


bool imcEditorCamera(EditorCamera &camera)
{
    auto position = camera.position;
    auto rotation = camera.rotation;
    bool bChanged = false;

    // Add camera control settings to UI
    if (ImGui::CollapsingHeader("Camera Controls")) {
        if (ImGui::DragFloat3("Camera Position", glm::value_ptr(position), 0.01f, -100.0f, 100.0f)) {
            bChanged = true;
        }
        if (ImGui::DragFloat3("Camera Rotation", glm::value_ptr(rotation), 1.f, -180.0f, 180.0f)) {
            bChanged = true;
        }
        ImGui::DragFloat("Move Speed", &camera.moveSpeed, 0.1f, 0.1f, 20.0f);
        ImGui::DragFloat("Rotation Speed", &camera.rotationSpeed, 0.01f, 0.01f, 1.0f);
        ImGui::Text("Hold right mouse button to rotate camera");
        ImGui::Text("WASD: Move horizontally, QE: Move vertically");
    }

    if (bChanged) {
        camera.setPositionAndRotation(position, rotation);
    }
    return bChanged;
}



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
        .windowProvider = windowProvider,
        .swapchainCI    = SwapchainCreateInfo{
               .imageFormat   = EFormat::R8G8B8A8_UNORM,
               .bVsync        = true,
               .minImageCount = 2,
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
            // color to present
            AttachmentDescription{
                .index          = 0,
                .format         = EFormat::R8G8B8A8_UNORM, // TODO: detect by device
                .samples        = ESampleCount::Sample_1,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::Undefined,
                .finalLayout    = EImageLayout::PresentSrcKHR,
            },
            // depth attachment
            AttachmentDescription{
                .index          = 1,
                .format         = EFormat::D32_SFLOAT, // TODO: detect by device
                .samples        = ESampleCount::Sample_1,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::Undefined,
                .finalLayout    = EImageLayout::DepthStencilAttachmentOptimal,
            },
        },
        .subpasses = {
            RenderPassCreateInfo::SubpassInfo{
                .subpassIndex     = 0,
                .colorAttachments = {
                    RenderPassCreateInfo::AttachmentRef{
                        .ref    = 0, // color attachment
                        .layout = EImageLayout::ColorAttachmentOptimal,
                    },
                },
                .depthAttachment = RenderPassCreateInfo::AttachmentRef{
                    .ref    = 1, // depth attachment
                    .layout = EImageLayout::DepthStencilAttachmentOptimal,
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

    swapchainImageSize = static_cast<int>(images.size());

    // TODO: maybe copy and cause destruction?
    // TODO: this should be a resources part of logical device or swapchain. And should be recreated when swapchain is recreated
    frameBuffers.resize(swapchainImageSize);

    for (size_t i = 0; i < images.size(); ++i)
    {
        frameBuffers[i] = VulkanFrameBuffer(vkRender, renderpass, vkSwapChain->getWidth(), vkSwapChain->getHeight());

        std::vector<std::shared_ptr<VulkanImage>> fbAttachments = {
            // color attachment
            VulkanImage::from(vkRender, images[i], surfaceFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
            VulkanImage::create(
                vkRender,
                ImageCreateInfo{
                    .format = DEPTH_FORMAT,
                    .extent = {
                        .width  = vkSwapChain->getWidth(),
                        .height = vkSwapChain->getHeight(),
                        .depth  = 1,
                    },
                    .mipLevels     = 1,
                    .samples       = ESampleCount::Sample_1,
                    .usage         = EImageUsage::DepthStencilAttachment,
                    .sharingMode   = ESharingMode::Exclusive,
                    .initialLayout = EImageLayout::Undefined,
                    // .finalLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                }),
        };

        frameBuffers[i].recreate(fbAttachments, vkSwapChain->getWidth(), vkSwapChain->getHeight());
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
        .pushConstants = {
            GraphicsPipelineLayoutCreateInfo::PushConstant{
                .offset     = 0,
                .size       = sizeof(glm::mat4) * 2, // dynamical allocated buffer
                .stageFlags = EShaderStage::Vertex,
            },
        },
        // .descriptorSetLayouts = {
        //     GraphicsPipelineLayoutCreateInfo::DescriptorSetLayout{
        //         .bindings = {
        //             GraphicsPipelineLayoutCreateInfo::DescriptorBinding{
        //                 .binding         = 0,
        //                 .descriptorType  = EPipelineDescriptorType::StorageBuffer,
        //                 .descriptorCount = 1,
        //                 .stageFlags      = EShaderStage::Vertex,
        //             },
        //         },
        //             },
        // } ,
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
            .shaderName        = "HelloCube.glsl",
            .bDeriveFromShader = false,
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(ya::Vertex),
                },
            },
            .vertexAttributes = {
                // (location=0) in vec3 aPos,
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 0,
                    .format     = EVertexAttributeFormat::Float3,
                    .offset     = offsetof(ya::Vertex, position),
                },
                //  texcoord
                VertexAttribute{
                    .bufferSlot = 0, // same buffer slot
                    .location   = 1,
                    .format     = EVertexAttributeFormat::Float2,
                    .offset     = offsetof(ya::Vertex, texCoord0),
                },
                // normal
                VertexAttribute{
                    .bufferSlot = 0, // same buffer slot
                    .location   = 2,
                    .format     = EVertexAttributeFormat::Float4,
                    .offset     = offsetof(ya::Vertex, normal),
                },
            },
        },
        .subPassRef = 0,

        // define what state need to dynamically modified in render pass execution
        .dynamicFeatures = EPipelineDynamicFeature::Scissor | // the imgui required this feature as I did not set the dynamical render feature
                           EPipelineDynamicFeature::Viewport,
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .frontFace   = EFrontFaceType::CounterClockWise, // GL
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable = true,
        },
        .colorBlendState = ColorBlendState{
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
        .viewportState = ViewportState{
            .viewports = {
                {
                    .x        = 0,
                    .y        = 0,
                    .width    = static_cast<float>(vkRender->getSwapChain()->getWidth()),
                    .height   = static_cast<float>(vkRender->getSwapChain()->getHeight()),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                },
            },
            .scissors = {Scissor{
                .offsetX = 0,
                .offsetY = 0,
                .width   = static_cast<uint32_t>(vkRender->getSwapChain()->getWidth()),
                .height  = static_cast<uint32_t>(vkRender->getSwapChain()->getHeight()),
            }},
        },
    });

    _firstGraphicsQueue = &vkRender->getGraphicsQueues()[0];
    _firstPresentQueue  = &vkRender->getPresentQueues()[0];

    initSemaphoreAndFence();


    std::vector<ya::Vertex> vertices;
    std::vector<uint32_t>   indices;
    ya::GeometryUtils::makeCube(
        -0.5,
        0.5,
        -0.5,
        0.5,
        -0.5,
        0.5,
        vertices,
        indices,
        true,
        true);

    vertexBuffer = VulkanBuffer::create(
        vkRender,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        sizeof(vertices[0]) * vertices.size(),
        vertices.data());
    indexBuffer = VulkanBuffer::create(
        vkRender,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        sizeof(indices[0]) * indices.size(),
        indices.data());
    indexSize = static_cast<uint32_t>(indices.size());

    vkRender->setDebugObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)vertexBuffer->getHandle(), "VertexBuffer");
    vkRender->setDebugObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)indexBuffer->getHandle(), "IndexBuffer");

    matModel = glm::mat4(1.0f);
    // we span at z = 3 and look at the origin ( right hand? and the cubes all place on xy plane )
    camera.setPosition(glm::vec3(0.0f, 0.0f, 3.0f));
    camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.setPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);



    // imgui init
    auto                      queue = _firstGraphicsQueue;
    ImGui_ImplVulkan_InitInfo initInfo{
        .ApiVersion     = vkRender->getApiVersion(),
        .Instance       = vkRender->getInstance(),
        .PhysicalDevice = vkRender->getPhysicalDevice(),
        .Device         = vkRender->getLogicalDevice(),
        .QueueFamily    = queue->getFamilyIndex(),
        .Queue          = queue->getHandle(),
        .DescriptorPool = nullptr,
        .RenderPass     = renderpass->getHandle(),
        .MinImageCount  = 2,
        .ImageCount     = static_cast<uint32_t>(vkRender->getSwapChain()->getImages().size()),
        .MSAASamples    = VK_SAMPLE_COUNT_1_BIT,

        // (Optional)
        .PipelineCache = vkRender->getPipelineCache(),
        .Subpass       = 0,

        // (Optional) Set to create internal descriptor pool instead of using DescriptorPool
        .DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE, // let imgui create it by itself internal

        // (Optional) Dynamic Rendering
        // Need to explicitly enable VK_KHR_dynamic_rendering extension to use this, even for Vulkan 1.3.
        .UseDynamicRendering = false,
#ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
        .PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfoKHR{},
#endif

        // (Optional) Allocation, Debugging
        .Allocator       = VK_NULL_HANDLE,
        .CheckVkResultFn = [](VkResult err) {
            if (err != VK_SUCCESS) {
                NE_CORE_ERROR("Vulkan error: {}", err);
            }
        },
        // Minimum allocation size. Set to 1024*1024 to satisfy zealous best practices validation layer and waste a little memory.
        .MinAllocationSize = static_cast<VkDeviceSize>(1024 * 1024),
    };
    imgui.init(vkRender->getNativeWindow<SDL_Window>(), initInfo);
}

void Neon::App::quit()
{
    auto *vkRender = static_cast<VulkanRender *>(_render);
    vkDeviceWaitIdle(vkRender->getLogicalDevice());

    imgui.shutdown();

    vertexBuffer.reset();
    indexBuffer.reset();

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

    static Uint64 lastTimeMS = SDL_GetTicks();
    while (bRunning) {

        Uint64 nowMS = SDL_GetTicks(); // return milliseconds
        float  dtSec = (nowMS - lastTimeMS) / 1000.0f;
        lastTimeMS   = nowMS;

        if (auto result = iterate(dtSec); result != 0) {
            break;
        }
    }

    return 0;
}


int Neon::App::onEvent(SDL_Event &event)
{
    EventProcessState ret = imgui.processEvents(event);
    if (ret != EventProcessState::Continue) {
        return 0;
    }
    inputManager.processEvent(event);


#pragma region sdl event match

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
#pragma endregion


    return 0;
};


FPSControl fpsCtrl;
int        Neon::App::iterate(float dt)
{

    SDL_Event evt;
    SDL_PollEvent(&evt);

    if (auto result = onEvent(evt); result != 0) {
        return 1;
    }

    dt += fpsCtrl.update(dt);

    onUpdate(dt);
    onDraw(dt);
    return 0;
}

void App::onUpdate(float dt)
{
    inputManager.update();

    static auto time = 0.f;
    time += dt;                       // dt is in milliseconds
    float speed = glm::radians(45.f); // 45 degrees per second
    float alpha = speed * time;       // Convert time from ms to seconds

    glm::quat rotX = glm::angleAxis(alpha, glm::vec3(1, 0, 0));
    glm::quat rotY = glm::angleAxis(alpha, glm::vec3(0, 1, 0));
    glm::quat rotZ = glm::angleAxis(alpha, glm::vec3(0, 0, 1));

    // xyz rotation
    glm::quat combinedRot = rotZ * rotY * rotX;

    // auto rotation by up-down and left-right
    glm::mat4 rot = glm::mat4_cast(combinedRot);
    matModel      = rot;

    camera.update(inputManager, dt); // Camera expects dt in seconds
}

void App::onDraw(float dt)
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
        frameFences[currentFrame],              // 等待上一帧渲完成
        // VK_NULL_HANDLE,                         // or 不传递 fence，避免双重等待问题
        imageIndex);


    // 2. begin command buffer
    VkCommandBuffer curCmdBuf = vkRender->getCommandBuffers()[imageIndex];
    vkResetCommandBuffer(curCmdBuf, 0); // Reset command buffer before recording
    begin(curCmdBuf);

    // 3 begin render pass && bind frame buffer
    // TODO: subpasses?
    std::vector<VkClearValue> clearValues{
        VkClearValue{.color = {{0.0f, 0.0f, 0.0f, 1.0f}}}, // Clear color: black
        VkClearValue{.depthStencil = {1.0f, 0}},           // Clear depth to 1.0
    };

    NE_CORE_ASSERT(
        clearValues.size() == renderpass->getCI().attachments.size(),
        "Clear values count must match attachment count!");

    renderpass->begin(curCmdBuf,
                      frameBuffers[imageIndex].getHandle(),
                      vkRender->getSwapChain()->getExtent(),
                      std::move(clearValues));


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

    VkViewport viewport{
        .x        = 0,
        .y        = 0,
        .width    = static_cast<float>(frameBuffers[imageIndex].width),
        .height   = static_cast<float>(frameBuffers[imageIndex].height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(curCmdBuf, 0, 1, &viewport);


    // Set scissor (required by imgui , and cause I must call this here)
    VkRect2D scissor{
        .offset = {0, 0},
        .extent = vkRender->getSwapChain()->getExtent(),
    };
    vkCmdSetScissor(curCmdBuf, 0, 1, &scissor);
#endif

#pragma region render

    // push constants: dynamical stack push, high performance, reduce descriptor usage (to ubo)
    struct PushConstantData
    {
        glm::mat4 viewProjection;
        glm::mat4 model;
    } pushData;

    pushData.viewProjection = camera.getViewProjectionMatrix();
    pushData.model          = matModel;

    vkCmdPushConstants(curCmdBuf,
                       defaultPipelineLayout->getHandle(),
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0,
                       sizeof(PushConstantData),
                       &pushData);

    // Bind vertex buffer
    VkBuffer vertexBuffers[] = {vertexBuffer->getHandle()};
    // current no need to support subbuffer
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(curCmdBuf, 0, 1, vertexBuffers, offsets);

    vkCmdBindIndexBuffer(curCmdBuf,
                         indexBuffer->getHandle(),
                         0,
                         VK_INDEX_TYPE_UINT32);

    // 8. draw triangle

    // vkCmdDraw(curCmdBuf, 3, 1, 0, 0);
    vkCmdDrawIndexed(curCmdBuf,
                     indexSize, // index count
                     9,         // instance count
                     0,         // first index
                     0,         // vertex offset, this for merge vertex buffer?
                     0          // first instance
    );

    imgui.beginFrame();
    if (ImGui::Begin("Test")) {
        float fps = 1.0f / dt;
        ImGui::Text("DeltaTime: %.2f ms , FPS: %.1f", dt * 1000.0f, fps);
        ImGui::Text("Current frame: %d", currentFrame);

        // Check if actual timing is way off from expected
        if (fps > 200.0f) {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "WARNING: VSync may not be working!");
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Possible causes:");
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "- High refresh rate monitor (144Hz/240Hz/1000Hz)");
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "- VSync disabled in GPU driver");
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "- Present mode not set to FIFO");
        }

        imcEditorCamera(camera);
        ImGui::End();
    }
    imgui.render();
    // imgui.submit(curCmdBuf, pipeline->getHandle());
    imgui.submit(curCmdBuf); // leave nullptr to let imgui use its pipeline
    imgui.endFrame();

#pragma endregion



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
    currentFrame = (currentFrame + 1) % swapchainImageSize;
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
    imageAvailableSemaphores.resize(swapchainImageSize);
    submittedSignalSemaphores.resize(swapchainImageSize);
    frameFences.resize(swapchainImageSize);

    // ✅ 循环创建每个飞行帧的同步对象
    for (uint32_t i = 0; i < swapchainImageSize; i++) {
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
    for (uint32_t i = 0; i < swapchainImageSize; i++) {
        vkDestroySemaphore(vkRender->getLogicalDevice(), imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(vkRender->getLogicalDevice(), submittedSignalSemaphores[i], nullptr);
        vkDestroyFence(vkRender->getLogicalDevice(), frameFences[i], nullptr);
    }
}

#pragma endregion


} // namespace Neon
