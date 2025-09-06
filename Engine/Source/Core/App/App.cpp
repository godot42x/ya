#include "App.h"

#include "Core/Camera.h"
#include "Core/Event.h"
#include "Core/FName.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/KeyCode.h"
#include "Core/MessageBus.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/Material/BaseMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/UnlitMaterialSystem.h"
#include "Render/Core/Material.h"


#include "Math/Geometry.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanDescriptorSet.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"



#include "Core/AssetManager.h"


#include "Render/Core/RenderTarget.h"
#include "Render/Mesh.h"
#include "Render/Render.h"
#include "Render/Render2D.h"



#include "ImGuiHelper.h"


#if defined(_WIN32)
    #include <windows.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <csignal>

#include "glm/glm.hpp"


#include "ECS/Entity.h"
#include "ECS/System/BaseMaterialSystem.h"



namespace ya
{

// Define the static member variable
App *App::_instance = nullptr;


VulkanRenderPass *renderpass;
// std::vector<VulkanFrameBuffer> frameBuffers;
RenderTarget *rt = nullptr;

std::vector<VkCommandBuffer> commandBuffers;

// std::vector<std::shared_ptr<VulkanImage>> depthImages;
const auto DEPTH_FORMAT = EFormat::D32_SFLOAT_S8_UINT;

// 手动设置多层cpu缓冲，让 cpu 在多个帧之间轮转, cpu and gpu can work in parallel
// but frame count should be limited and considered  with performance
static uint32_t currentFrameIdx = 0;
int             fps             = 60;



std::shared_ptr<Mesh> cubeMesh;

const char *faceTexturePath = "Engine/Content/TestTextures/face.png";
const char *uv1TexturePath  = "Engine/Content/TestTextures/uv1.png";


ImguiState imgui;

VkClearValue colorClearValue = {
    .color = {
        .float32 = {
            0,
            0,
            0,
            1,
        },
    },
};
VkClearValue depthClearValue = {
    .depthStencil = {
        .depth   = 1,
        .stencil = 0},
};

#pragma region misc

struct FPSControl
{
    float fps     = 0.0f;
    bool  bEnable = false;

    static constexpr float defaultFps = 60.f;

    float fpsLimit = defaultFps;
    float wantedDT = 1.f / defaultFps;

    float update(float &dt)
    {
        if (!bEnable) {
            return 0;
        }

        if (dt < wantedDT)
        {
            float delayTimeSec = wantedDT - dt;
            // YA_CORE_INFO("FPS limit exceeded. Delaying for {} ms", delayTime);
            SDL_Delay(static_cast<Uint32>(delayTimeSec * 1000));
            return delayTimeSec;
        }

        return 0;
    }

    void setFPSLimit(float limit)
    {
        fpsLimit = limit;
        wantedDT = 1.f / fpsLimit;
    }
};

static FPSControl fpsCtrl;

void imcFpsControl(FPSControl &fpsCtrl)
{
    if (ImGui::CollapsingHeader("FPS Control", ImGuiTreeNodeFlags_DefaultOpen)) {

        ImGui::Text("FPS Limit: %.1f", fpsCtrl.fpsLimit);

        static float newFpsLimit = fpsCtrl.fpsLimit;
        ImGui::PushItemWidth(100.0f);
        ImGui::InputFloat("New: ", &newFpsLimit, 10.0f, 10.0f, "%.1f");
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("Confirm")) {
            fpsCtrl.setFPSLimit(newFpsLimit);
            fps = (int)newFpsLimit;
        }

        ImGui::Checkbox("Enable FPS Control", &fpsCtrl.bEnable);
    }
}

bool imcEditorCamera(FreeCamera &camera)
{
    auto position = camera._position;
    auto rotation = camera._rotation;
    bool bChanged = false;

    // Add camera control settings to UI
    if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::DragFloat3("Camera Position", glm::value_ptr(position), 0.01f, -100.0f, 100.0f)) {
            bChanged = true;
        }
        if (ImGui::DragFloat3("Camera Rotation", glm::value_ptr(rotation), 1.f, -180.0f, 180.0f)) {
            bChanged = true;
        }
        ImGui::DragFloat("Move Speed", &camera._moveSpeed, 0.1f, 0.1f, 20.0f);
        ImGui::DragFloat("Rotation Speed", &camera._rotationSpeed, 1.f, 10.f, 180.f);
        ImGui::Text("Hold right mouse button to rotate camera");
        ImGui::Text("WASD: Move horizontally, QE: Move vertically");
    }


    if (bChanged) {
        camera.setPositionAndRotation(position, rotation);
    }
    return bChanged;
}

void imcClearValues()
{
    if (ImGui::CollapsingHeader("Clear Values", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit4("Color Clear Value", colorClearValue.color.float32);
        ImGui::DragFloat("Depth Clear Value", &depthClearValue.depthStencil.depth, 0.01f, 0.0f, 1.0f);
    }
}

#pragma endregion

void App::init(AppCreateInfo ci)
{
    YA_PROFILE_FUNCTION();
    _ci = ci;
    YA_CORE_ASSERT(_instance == nullptr, "Only one instance of App is allowed");
    _instance = this;

    // register terminal  C-c signal
    {

#if !defined(_WIN32)
        auto handler = [](int signal) {
            if (!App::_instance) {
                return;
            }
            YA_CORE_INFO("Received signal: {}", signal);

            switch (signal) {
            case SIGINT:
            case SIGTERM:
            {
                App::_instance->requestQuit();
            } break;
            default:
                break;
            }
        };

        std::signal(SIGINT, handler);  // Ctrl+C
        std::signal(SIGTERM, handler); // Termination request
#else
        // linux: will continue to execute after handle the signal
        // Windows: need 用SetConsoleCtrlHandler来拦截并阻止默认退出
        SetConsoleCtrlHandler(
            [](DWORD dwCtrlType) -> BOOL {
                switch (dwCtrlType) {
                case CTRL_C_EVENT:
                case CTRL_BREAK_EVENT:
                    YA_CORE_INFO("Received Ctrl+C, requesting graceful shutdown...");
                    if (App::_instance) {
                        App::_instance->requestQuit();
                    }
                    return true; // 返回TRUE阻止默认的终止行为
                case CTRL_CLOSE_EVENT:
                case CTRL_LOGOFF_EVENT:
                case CTRL_SHUTDOWN_EVENT:
                    YA_CORE_INFO("Received system shutdown event");
                    if (App::_instance) {
                        App::_instance->requestQuit();
                    }
                    return true; // 返回TRUE阻止默认的终止行为
                };

                return FALSE; // 对于其他事件，使用默认处理
            },
            TRUE);
#endif
    }


    {
        YA_PROFILE_SCOPE("App Init Subsystems");
        Logger::init();
        FileSystem::init();
        NameRegistry::init(); // Initialize FName registry
        AssetManager::init();
        MaterialFactory::init();
    }

    {
        YA_PROFILE_SCOPE("Inheritance Init");
        onInit(ci);
    }

    currentRenderAPI          = ERenderAPI::Vulkan;
    std::string currentShader = "Test/BaseMaterial.glsl";

    auto shaderProcessor = ShaderProcessorFactory()
                               .withProcessorType(ShaderProcessorFactory::EProcessorType::GLSL)
                               .withShaderStoragePath("Engine/Shader/GLSL")
                               .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
                               .FactoryNew<GLSLProcessor>();

    _shaderStorage = std::make_shared<ShaderStorage>(shaderProcessor);
    _shaderStorage->load("Test/Unlit.glsl");
    _shaderStorage->load(currentShader);
    _shaderStorage->load("Sprite2D.glsl");


    RenderCreateInfo renderCI{
        .renderAPI   = currentRenderAPI,
        .swapchainCI = SwapchainCreateInfo{
            .imageFormat   = EFormat::R8G8B8A8_UNORM,
            .presentMode   = EPresentMode::FIFO, // vsync
            .bVsync        = true,
            .minImageCount = 3,
            .width         = static_cast<uint32_t>(_ci.width),
            .height        = static_cast<uint32_t>(_ci.height),
        },
    };
    _render        = IRender::create(renderCI);
    auto *vkRender = dynamic_cast<VulkanRender *>(_render);


    /**
      In vulkan:
      1. Create instance
      2. Create surface
      3. Find physical device
      4. Create logical device
      5. Create swap chain
      6. Other device resources: command pool{command buffers}, fences, semaphores, etc.
     */
    vkRender->init(renderCI);

    vkRender->getWindowProvider()->getWindowSize(_windowSize.x, _windowSize.y);


    // WHY: my ARC 730M must need to recreate with FIFO again to enable vsync
    vkRender->getSwapChain()->setVsync(renderCI.swapchainCI.bVsync);

    constexpr auto _sampleCount = ESampleCount::Sample_1; // TODO: support MSAA
    // MARK: RenderPass

    /**
      In Vulkan:
        Create render pass and subpass
        define all attachments,
        input/color/depth/resolved attachment ref from all attachments
        and each subpasses dependencies (source -> next)
     */
    renderpass = new VulkanRenderPass(vkRender);
    renderpass->recreate(RenderPassCreateInfo{
        .attachments = {
            // color to present
            AttachmentDescription{
                .index          = 0,
                .format         = EFormat::R8G8B8A8_UNORM, // TODO: detect by device
                .samples        = ESampleCount::Sample_1,  // first present attachment cannot be multi-sampled
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::Undefined,
                .finalLayout    = EImageLayout::PresentSrcKHR,
                .usage          = EImageUsage::ColorAttachment,
            },
            // depth attachment
            AttachmentDescription{
                .index          = 1,
                .format         = DEPTH_FORMAT,
                .samples        = _sampleCount,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::Undefined,
                .finalLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                .usage          = EImageUsage::DepthStencilAttachment,
            },
        },
        .subpasses = {
            RenderPassCreateInfo::SubpassInfo{
                .subpassIndex     = 0,
                .inputAttachments = {},
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
                .resolveAttachment = {},
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
    vkRender->allocateCommandBuffers(images.size(), commandBuffers);

    // use the RT instead of framebuffers directly
    rt = new RenderTarget(renderpass);
    rt->addMaterialSystem<BaseMaterialSystem>();
    rt->addMaterialSystem<UnlitMaterialSystem>();

    _firstGraphicsQueue = &vkRender->getGraphicsQueues()[0];
    _firstPresentQueue  = &vkRender->getPresentQueues()[0];



#pragma region ImGui Init
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
        .PipelineCache = nullptr,
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
        .Allocator       = vkRender->getAllocator(),
        .CheckVkResultFn = [](VkResult err) {
            if (err != VK_SUCCESS) {
                YA_CORE_ERROR("Vulkan error: {}", err);
            }
        },
        // Minimum allocation size. Set to 1024*1024 to satisfy zealous best practices validation layer and waste a little memory.
        .MinAllocationSize = static_cast<VkDeviceSize>(1024 * 1024),
    };
    imgui.init(vkRender->getNativeWindow<SDL_Window>(), initInfo);
#pragma endregion


    Render2D::init(_render, renderpass);



    loadScene(ci.defaultScenePath);
    // wait something done
    vkDeviceWaitIdle(vkRender->getLogicalDevice());
}

void App::onInit(AppCreateInfo ci)
{
    auto &bus = MessageBus::get();
    bus.subscribe<WindowResizeEvent>(this, &App::onWindowResized);
    bus.subscribe<KeyReleasedEvent>(this, &App::onKeyReleased);
    bus.subscribe<MouseScrolledEvent>(this, &App::onMouseScrolled);
}


// MARK: QUIT
void ya::App::quit()
{
    auto *vkRender = static_cast<VulkanRender *>(_render);
    vkDeviceWaitIdle(vkRender->getLogicalDevice());

    unloadScene();
    MaterialFactory::get()->destroy();

    Render2D::destroy();
    imgui.shutdown();

    // pipeline->cleanup();
    // delete pipeline;

    // defaultPipelineLayout->cleanup();
    // delete defaultPipelineLayout;
    // descriptorSetLayout0.reset();
    // descriptorPool.reset();


    delete rt;
    delete renderpass;

    AssetManager::get()->cleanup();
    _render->destroy();

    delete _render;
}



int ya::App::run()
{

    _startTime = std::chrono::steady_clock::now();
    _lastTime  = _startTime;

    while (bRunning) {

        time_point_t now        = clock_t::now();
        auto         dtMicroSec = std::chrono::duration_cast<std::chrono::microseconds>(now - _lastTime).count();
        float        dtSec      = (float)((double)dtMicroSec / 1000000.0);
        dtSec                   = std::max(dtSec, 0.0001f);
        _lastTime               = now;


        if (auto result = iterate(dtSec); result != 0) {
            break;
        }
    }

    return 0;
}


int ya::App::processEvent(SDL_Event &event)
{
    // TODO: refactor event system
    //  use bus to dispatch event to each system or subsystems is great
    // but  there is not serial order for processing events
    // for example: imgui want to capture mouse/keyboard event first
    // if subscribe it in imgui's obj, it may be after other systems

    // YA_CORE_TRACE("Event processed: {}", event.type);
    EventProcessState ret = imgui.processEvents(event);
    if (ret != EventProcessState::Continue) {
        return 0;
    }
    inputManager.processEvent(event);

    auto &bus = MessageBus::get();

#pragma region Sdl Event

    switch (SDL_EventType(event.type))
    {
    case SDL_EVENT_FIRST:
        break;
    case SDL_EVENT_QUIT:
    {
        requestQuit();
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
        break;
    case SDL_EVENT_WINDOW_RESIZED:
    {
        bus.publish(WindowResizeEvent{event.window.windowID, event.window.data1, event.window.data2});

    } break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    {
        bus.publish(WindowFocusEvent(event.window.windowID));
    } break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    {
        bus.publish(WindowFocusLostEvent(event.window.windowID));
    } break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    {
        requestQuit();
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
    {
        KeyPressedEvent ev;
        ev._keyCode = (enum EKey::T)event.key.key;
        ev._mod     = event.key.mod;
        ev.bRepeat  = event.key.repeat; // SDL3中的repeat字段
        bus.publish(ev);
    } break;
    case SDL_EVENT_KEY_UP:
    {
        KeyReleasedEvent ev;
        ev._keyCode = static_cast<EKey::T>(event.key.key);
        ev._mod     = event.key.mod;
        bus.publish(ev);
    } break;
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_KEYMAP_CHANGED:
    case SDL_EVENT_KEYBOARD_ADDED:
    case SDL_EVENT_KEYBOARD_REMOVED:
    case SDL_EVENT_TEXT_EDITING_CANDIDATES:
    case SDL_EVENT_MOUSE_MOTION:
    {
        MouseMoveEvent ev(event.motion.x, event.motion.y);
        // TODO: relative to window and global's motion size?
        // , event.motion.xrel, event.motion.yrel);
        bus.publish(ev);
    } break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
        MouseButtonPressedEvent ev(event.button.button);
        bus.publish(ev);

    } break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        MouseButtonReleasedEvent ev(event.button.button);
        bus.publish(ev);

    } break;
    case SDL_EVENT_MOUSE_WHEEL:
    {
        MouseScrolledEvent ev;
        ev._offsetX = event.wheel.x;
        ev._offsetY = event.wheel.y;
        bus.publish(ev);
    } break;
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


int ya::App::iterate(float dt)
{
    SDL_Event evt;
    SDL_PollEvent(&evt);

    if (auto result = processEvent(evt); result != 0) {
        return 1;
    }

    dt += fpsCtrl.update(dt);

    if (!_bPause) {
        onUpdate(dt);
    }
    onRender(dt);
    taskManager.update();
    ++_frameIndex;
    return 0;
}


void App::onUpdate(float dt)
{
    inputManager.update();

    // static auto time = 0.f;
    // time += dt;                       // dt is in milliseconds
    // float speed = glm::radians(45.f); // 45 degrees per second
    // float alpha = speed * time;       // Convert time from ms to seconds

    // glm::quat rotX = glm::angleAxis(alpha, glm::vec3(1, 0, 0));
    // glm::quat rotY = glm::angleAxis(alpha, glm::vec3(0, 1, 0));
    // glm::quat rotZ = glm::angleAxis(alpha, glm::vec3(0, 0, 1));

    // // xyz rotation
    // glm::quat combinedRot = rotZ * rotY * rotX;

    // // auto rotation by up-down and left-right
    // glm::mat4 rot = glm::mat4_cast(combinedRot);

    rt->setColorClearValue(colorClearValue);
    rt->setDepthStencilClearValue(depthClearValue);
    rt->onUpdate(dt);

    auto cam = rt->getCameraMut();

    camera.update(inputManager, dt); // Camera expects dt in seconds
    float sensitivity = 0.1f;
    if (cam && cam->hasComponent<CameraComponent>()) {
        auto        cc  = cam->getComponent<CameraComponent>();
        const auto &ext = rt->_extent;
        cc->setAspectRatio(static_cast<float>(ext.width) / static_cast<float>(ext.height));
        auto &inputManger = App::get()->inputManager;

        if (inputManger.isMouseButtonPressed(EMouse::Right)) {
            glm::vec2 mouseDelta = inputManger.getMouseDelta();
            if (glm::length(mouseDelta) > 0.0f) {
                auto tc = cam->getComponent<TransformComponent>();

                float yaw   = tc->_rotation.x;
                float pitch = tc->_rotation.y;
                yaw -= mouseDelta.x * sensitivity;
                pitch -= mouseDelta.y * sensitivity;
                if (pitch > 89.f) {
                    pitch = 89.f;
                }
                else if (pitch < -89.f) {
                    pitch = -89.f;
                }

                tc->_rotation.x = yaw;
                tc->_rotation.y = pitch;
            }
        }
        glm::vec2 scrollDelta = inputManger.getMouseScrollDelta();
        cc->_distance -= scrollDelta.y * 0.1f;
    }

    auto vkRender = static_cast<VulkanRender *>(_render);
    auto cmdBuf   = vkRender->beginIsolateCommands();
    vkRender->endIsolateCommands(cmdBuf);
}

void App::onRender(float dt)
{
    auto vkRender = static_cast<VulkanRender *>(_render);
    // vkDeviceWaitIdle(vkRender->getLogicalDevice());
    // SDL_Delay(1000 / 30); // Simulate frame time, remove in production

    if (_windowSize.x <= 0 || _windowSize.y <= 0) {
        YA_CORE_INFO("{}x{}: Window minimized, skipping frame", _windowSize.x, _windowSize.y);
        return;
    }

    int32_t imageIndex = -1;
    vkRender->begin(&imageIndex);

    // 2. begin command buffer
    VkCommandBuffer curCmdBuf = commandBuffers[imageIndex];
    vkResetCommandBuffer(curCmdBuf, 0); // Reset command buffer before recording
    VulkanCommandPool::begin(curCmdBuf);


    // 3 begin render pass && bind frame buffer
    // TODO: subpasses?
    rt->begin(curCmdBuf);
    rt->onRender(curCmdBuf);

    static glm::vec3 pos1 = glm::vec3(0.f, 0, 0);
    static glm::vec3 pos2 = glm::vec3(100.f, 100.f, 0.f);
    static glm::vec3 pos3 = glm::vec3(500.f, 500.f, 0.f);
    static glm::vec3 pos4 = glm::vec3(-500.f, -500.f, 0.f);

    // MARK: Render2D
    Render2D::begin(curCmdBuf);
    // Render2D::makeSprite(pos1, glm::vec2(100, 100), glm::vec4(1.0f));
    // Render2D::makeSprite(pos2, {100, 100}, glm::vec4(1, 0, 0, 1));
    // Render2D::makeSprite(pos3, {100, 100}, glm::vec4(0, 1, 0, 1));
    // Render2D::makeSprite(pos4, {100, 100}, glm::vec4(0, 0, 1, 1));
    // Render2D::makeSprite({10, 0, 0}, glm::
    // Render2D::makeSprite({20, 0, 0}, glm::vec2(10, 10), glm::vec4(1.0f));
    // int count = 10;
    // for (int i = 0; i < count; i++) {
    //     Render2D::makeSprite(
    //         glm::vec3(i * 100, i * 100, 0),
    //         glm::vec2(100.0f, 100.0f),
    //         glm::vec4(sin(i), cos(i), sin(float(i)), 1));
    // }
    Render2D::end();


#pragma region ImGui
    imgui.beginFrame();
    {
        if (ImGui::CollapsingHeader("Render 2D Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("pos1", glm::value_ptr(pos1), 0.1f);
            ImGui::DragFloat3("pos2", glm::value_ptr(pos2), 1.f);
            ImGui::DragFloat3("pos3", glm::value_ptr(pos3), 1.f);
            ImGui::DragFloat3("pos4", glm::value_ptr(pos4), 1.f);
            Render2D::onImGui();
        }


        if (ImGui::CollapsingHeader("Context", ImGuiTreeNodeFlags_DefaultOpen)) {
            float fps = 1.0f / dt;
            ImGui::Text("Frame: %d, DeltaTime: %.1f ms,\t FPS: %.1f", _frameIndex, dt * 1000.0f, fps);
            static int count = 0;
            if (ImGui::Button(std::format("Click Me ({})", count).c_str())) {
                count++;
                YA_CORE_INFO("=====================================");
            }

            // ImGui::DragFloat("Texture Mix Alpha", &pushData.textureMixAlpha, 0.01, 0, 1);

            bool bVsync = vkRender->getSwapChain()->bVsync;
            if (ImGui::Checkbox("VSync", &bVsync)) {
                taskManager.registerFrameTask([vkRender, bVsync]() {
                    // TODO :bind dirty link
                    vkRender->getSwapChain()->setVsync(bVsync);
                });
            }
        }
        onRenderGUI();


        imcEditorCamera(camera);
        imcClearValues();
        // imcFpsControl(fpsCtrl);
        // static bool bItemPicker = false;
        // ImGui::Checkbox("Debug Picker", &bItemPicker);
        // if (bItemPicker) {
        //     ImGui::DebugStartItemPicker();
        // }
        static bool bDarkMode = true;
        if (ImGui::Checkbox("Dark Mode", &bDarkMode)) {
            if (bDarkMode) {
                ImGui::StyleColorsDark();
            }
            else {
                ImGui::StyleColorsLight();
            }
        }
        if (auto *firstCube = _scene->getEntityByID(1)) {
            auto tc = firstCube->getComponent<TransformComponent>();

            if (ImGui::CollapsingHeader("First Cube Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Position", glm::value_ptr(tc->_position), 0.1f);
                ImGui::DragFloat3("Rotation", glm::value_ptr(tc->_rotation), 1.f);
                ImGui::DragFloat3("Scale", glm::value_ptr(tc->_scale), 0.1f, 0.1f);
            }

            tc->bDirty = true;
        }

        static int sampler = 0;
        if (ImGui::Combo("Default Sampler", &sampler, "linear\0nearest\0\0")) {
            _defaultSampler = (sampler == 0) ? _linearSampler : _nearestSampler;
        }

        if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (uint32_t i = 0; i < _materials.size(); i++) {
                auto mat   = _materials[i];
                auto base  = ya::type_index_v<BaseMaterial>;
                auto unlit = ya::type_index_v<UnlitMaterial>;

                ImGui::PushID(std::format("Material{}", i).c_str());
                if (mat->getTypeID() == unlit) {
                    auto *unlitMat = static_cast<UnlitMaterial *>(mat);
                    if (ImGui::CollapsingHeader(std::format("Material{} ({})", i, unlitMat->getLabel()).c_str())) {
                        bool bDirty = false;
                        bDirty |= ImGui::DragFloat3("Base Color0", glm::value_ptr(unlitMat->uMaterial.baseColor0), 0.1f);
                        bDirty |= ImGui::DragFloat3("Base Color1", glm::value_ptr(unlitMat->uMaterial.baseColor1), 0.1f);
                        bDirty |= ImGui::DragFloat("Mix Value", &unlitMat->uMaterial.mixValue, 0.01f, 0.0f, 1.0f);
                        for (uint32_t i = 0; i < mat->_textures.size(); i++) {
                            if (ImGui::CollapsingHeader(std::format("Texture{}", i).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                                bDirty |= ImGui::Checkbox(std::format("Texture{}  Enable", i).c_str(), &mat->_textures[i].bEnable);
                                bDirty |= ImGui::DragFloat2(std::format("Texture{} Offset", i).c_str(), glm::value_ptr(mat->_textures[i].uvTranslation), 0.01f);
                                bDirty |= ImGui::DragFloat(std::format("Texture{} Scale", i).c_str(), glm::value_ptr(mat->_textures[i].uvScale), 0.01f, 0.01f, 10.0f);
                                static constexpr const auto pi = glm::pi<float>();
                                bDirty |= ImGui::DragFloat(std::format("Texture{} Rotation", i).c_str(), &mat->_textures[i].uvRotation, pi / 3600, -pi, pi);
                            }
                        }
                        if (bDirty) {
                            unlitMat->setParamDirty(true);
                        }
                    }
                }
                else if (mat->getTypeID() == base)
                {
                    auto *baseMat = static_cast<BaseMaterial *>(mat);
                    if (ImGui::CollapsingHeader(std::format("Material{} ({})", i, baseMat->getLabel()).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        // use push constant to change color type, no need to mark dirty
                        int colorType = static_cast<int>(baseMat->colorType);
                        if (ImGui::Combo("Color Type", &colorType, "Normal\0Texcoord\0\0")) {
                            baseMat->colorType = static_cast<BaseMaterial::EColor>(colorType);
                        }
                    }
                }
                ImGui::PopID();
            }
        }
    }
    imgui.endFrame();
    imgui.render();
    // imgui.submit(curCmdBuf, pipeline->getHandle());
    imgui.submit(curCmdBuf, nullptr); // leave nullptr to let imgui use its pipeline


#pragma endregion

    // TODO: subpasses?
    rt->end(curCmdBuf);
    VulkanCommandPool::end(curCmdBuf);

    vkRender->end(imageIndex, {curCmdBuf});
}


void App::onRenderGUI()
{
    rt->onRenderGUI();
}

// MARK: Scene
bool App::loadScene(const std::string &path)
{
    if (_scene)
    {
        unloadScene();
    }

    _scene = std::make_unique<Scene>();
    onSceneInit(_scene.get());
    return true;
}

bool App::unloadScene()
{
    if (_scene)
    {
        onSceneDestroy(_scene.get());
        _scene.reset();
    }
    cubeMesh.reset();

    _whiteTexture.reset();
    _blackTexture.reset();
    _multiPixelTexture.reset();
    return true;
}

void App::onSceneInit(Scene *scene)
{
    std::vector<ya::Vertex> vertices;
    std::vector<uint32_t>   indices;
    ya::GeometryUtils::makeCube(-0.5, 0.5, -0.5, 0.5, -0.5, 0.5, vertices, indices, true, true);
    cubeMesh = std::make_shared<Mesh>(vertices, indices, "cube");



    _linearSampler = std::make_shared<VulkanSampler>(
        SamplerCreateInfo{
            .label         = "default",
            .minFilter     = EFilter::Linear,
            .magFilter     = EFilter::Linear,
            .mipmapMode    = ESamplerMipmapMode::Linear,
            .addressModeU  = ESamplerAddressMode::Repeat,
            .addressModeV  = ESamplerAddressMode::Repeat,
            .addressModeW  = ESamplerAddressMode::Repeat,
            .mipLodBias    = 0.0f,
            .maxAnisotropy = 1.0f,
        });
    _nearestSampler = std::make_shared<VulkanSampler>(
        SamplerCreateInfo{
            .label         = "nearest",
            .minFilter     = EFilter::Nearest,
            .magFilter     = EFilter::Nearest,
            .mipmapMode    = ESamplerMipmapMode::Nearest,
            .addressModeU  = ESamplerAddressMode::Repeat,
            .addressModeV  = ESamplerAddressMode::Repeat,
            .addressModeW  = ESamplerAddressMode::Repeat,
            .mipLodBias    = 0.0f,
            .maxAnisotropy = 1.0f,
        });
    _defaultSampler = _linearSampler;

    using color_t = ColorRGBA<uint8_t>;
    color_t white{.r = 255, .g = 255, .b = 255, .a = 255};
    color_t black{.r = 0, .g = 0, .b = 0, .a = 255};
    color_t red{.r = 255, .g = 0, .b = 0, .a = 255};
    color_t green{.r = 0, .g = 255, .b = 0, .a = 255};
    color_t blue{.r = 0, .g = 0, .b = 255, .a = 255};
    _whiteTexture      = makeShared<ya::Texture>(1, 1, std::vector<color_t>{white});
    _blackTexture      = makeShared<ya::Texture>(1, 1, std::vector<color_t>{black});
    _multiPixelTexture = makeShared<ya::Texture>(2, 2, std::vector<color_t>{
                                                           white,
                                                           black,
                                                           white,
                                                           black,
                                                       }); // 2x2 texture with 4 different colors
    AssetManager::get()->loadTexture("face", faceTexturePath);
    AssetManager::get()->loadTexture("uv1", uv1TexturePath);

    _materials.clear();


    // we span at z = 3 and look at the origin ( right hand? and the cubes all place on xy plane )
    camera.setPosition(glm::vec3(0.0f, 0.0f, 3.0f));
    camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.setPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);


    auto *baseMaterial0      = MaterialFactory::get()->createMaterial<BaseMaterial>("base0");
    auto *baseMaterial1      = MaterialFactory::get()->createMaterial<BaseMaterial>("base1");
    baseMaterial0->colorType = BaseMaterial::EColor::Normal;
    baseMaterial1->colorType = BaseMaterial::EColor::Texcoord;
    _materials.push_back(baseMaterial0);
    _materials.push_back(baseMaterial1);
    int baseMaterialCount = _materials.size();


    auto *unlitMaterial0 = MaterialFactory::get()->createMaterial<UnlitMaterial>("unlit0");
    unlitMaterial0->setTextureView(UnlitMaterial::BaseColor0, _whiteTexture.get(), _defaultSampler.get());
    unlitMaterial0->setTextureView(UnlitMaterial::BaseColor1, _multiPixelTexture.get(), _defaultSampler.get());
    unlitMaterial0->setTextureViewEnable(UnlitMaterial::BaseColor0, true);
    unlitMaterial0->setTextureViewEnable(UnlitMaterial::BaseColor1, true);
    unlitMaterial0->setMixValue(0.5);

    auto *unlitMaterial1 = MaterialFactory::get()->createMaterial<UnlitMaterial>("unlit1");
    unlitMaterial1->setTextureView(UnlitMaterial::BaseColor0, _blackTexture.get(), _defaultSampler.get());
    unlitMaterial1->setTextureViewEnable(UnlitMaterial::BaseColor0, true);
    unlitMaterial1->setTextureView(UnlitMaterial::BaseColor1, AssetManager::get()->getTextureByName("face").get(), _defaultSampler.get());
    unlitMaterial1->setTextureViewEnable(UnlitMaterial::BaseColor1, true);
    unlitMaterial1->setMixValue(0.5);

    auto *unlitMaterial2 = MaterialFactory::get()->createMaterial<UnlitMaterial>("unlit2");
    unlitMaterial2->setTextureView(UnlitMaterial::BaseColor0, AssetManager::get()->getTextureByName("uv1").get(), _defaultSampler.get());
    unlitMaterial2->setTextureView(UnlitMaterial::BaseColor1, _blackTexture.get(), _defaultSampler.get());
    unlitMaterial2->setTextureViewEnable(UnlitMaterial::BaseColor0, true);
    unlitMaterial2->setTextureViewEnable(UnlitMaterial::BaseColor1, true);
    unlitMaterial2->setMixValue(0.5);


    _materials.push_back(unlitMaterial0);
    _materials.push_back(unlitMaterial1);
    _materials.push_back(unlitMaterial2);


    float offset   = 3.f;
    float rotation = 10.f;
    int   count    = 100;
    int   alpha    = (int)std::round(std::pow(count, 1.0 / 3.0));
    YA_CORE_DEBUG("Creating {} entities ({}x{}x{})", alpha * alpha * alpha, alpha, alpha, alpha);

    int index            = 0;
    int maxMaterialIndex = _materials.size();
    for (int i = 0; i < alpha; ++i) {
        for (int j = 0; j < alpha; ++j) {
            for (int k = 0; k < alpha; ++k) {
                auto cube = scene->createEntity(std::format("Cube_{}_{}_{}", i, j, k));
                {
                    auto v  = glm::vec3(i, j, k);
                    auto tc = cube->addComponent<TransformComponent>();
                    tc->setPosition(offset * v);
                    tc->setRotation(rotation * v);
                    float alpha = std::sin(glm::radians(15.f * (float)(i + j + k)));
                    tc->setScale(glm::vec3(alpha));

                    int materialIndex = index % maxMaterialIndex;
                    ++index;
                    if (materialIndex < baseMaterialCount) {
                        // use base material
                        auto bmc = cube->addComponent<BaseMaterialComponent>();
                        auto mat = static_cast<BaseMaterial *>(_materials[materialIndex]);
                        YA_CORE_ASSERT(mat, "WTH");
                        bmc->addMesh(cubeMesh.get(), mat);
                    }
                    else {
                        // use unlit material
                        auto umc = cube->addComponent<UnlitMaterialComponent>();
                        auto mat = static_cast<UnlitMaterial *>(_materials[materialIndex]);
                        YA_CORE_ASSERT(mat, "WTH");
                        umc->addMesh(cubeMesh.get(), mat);
                    }
                }
            }
        }
    }

    auto cam = scene->createEntity("Camera");
    cam->addComponent<TransformComponent>();
    cam->addComponent<CameraComponent>();
    cam->addComponent<BaseMaterialComponent>();
    rt->setCamera(cam);

    YA_CORE_ASSERT(scene->getRegistry().any_of<CameraComponent>(cam->getHandle()), "WTH");
    YA_CORE_ASSERT(cam->hasComponent<CameraComponent>(), "???");

    auto cc    = cam->getComponent<CameraComponent>();
    auto owner = cc->getOwner();
    YA_CORE_ASSERT(owner == cam, "WTH");
}

bool App::onWindowResized(const WindowResizeEvent &event)
{
    auto  w           = event.GetWidth();
    auto  h           = event.GetHeight();
    float aspectRatio = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
    YA_CORE_DEBUG("Window resized to {}x{}, aspectRatio: {} ", w, h, aspectRatio);
    camera.setAspectRatio(aspectRatio);
    _windowSize = {w, h};
    return false;
}

bool App::onKeyReleased(const KeyReleasedEvent &event)
{
    if (event.getKeyCode() == EKey::Escape) {
        YA_CORE_INFO("{}", event.toString());
        requestQuit();
    }
    return true;
}

bool App::onMouseScrolled(const MouseScrolledEvent &event)
{
    float sensitivity = 0.5f;
    auto  cam         = rt->getCameraMut();
    auto  cc          = cam->getComponent<CameraComponent>();
    // up is +, and down is -, up is close so -=
    cc->_distance = cc->_distance -= event._offsetY * sensitivity;
    return false;
}



} // namespace ya
