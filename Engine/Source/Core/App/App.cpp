#include "App.h"
#include "Core/EditorCamera.h"
#include "Core/FName.h"
#include "Core/FileSystem/FileSystem.h"

#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Math/Geometry.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanDescriptorSet.h"
#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"

#include "Core/AssetManager.h"


#include "Render/Core/RenderTarget.h"
#include "Render/Core/Texture.h"
#include "Render/Render.h"
#include "render/Mesh.h"


#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>

#include "glm/glm.hpp"


#include "ECS/Entity.h"
#include "ECS/System/BaseMaterialSystem.h"



namespace ya
{

// Define the static member variable
App *App::_instance = nullptr;


VulkanRenderPass *renderpass;
// std::vector<VulkanFrameBuffer> frameBuffers;
RenderTarget *renderTarget = nullptr;

std::vector<VkCommandBuffer> commandBuffers;

// std::vector<std::shared_ptr<VulkanImage>> depthImages;
const auto DEPTH_FORMAT = EFormat::D32_SFLOAT_S8_UINT;

// 手动设置多层cpu缓冲，让 cpu 在多个帧之间轮转, cpu and gpu can work in parallel
// but frame count should be limited and considered  with performance
static uint32_t currentFrameIdx = 0;
int             fps             = 60;



std::shared_ptr<Mesh> cubeMesh;
bool                  bRotating = true;

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

bool imcEditorCamera(EditorCamera &camera)
{
    auto position = camera.position;
    auto rotation = camera.rotation;
    bool bChanged = false;

    // Add camera control settings to UI
    if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
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
    _ci = ci;
    YA_CORE_ASSERT(_instance == nullptr, "Only one instance of App is allowed");
    _instance = this;

    YA_PROFILE_FUNCTION();
    Logger::init();
    FileSystem::init();
    NameRegistry::init(); // Initialize FName registry
    AssetManager::init();

    onInit(ci);

    currentRenderAPI          = ERenderAPI::Vulkan;
    std::string currentShader = "Test/BaseMaterial.glsl";

    auto shaderProcessor = ShaderProcessorFactory()
                               .withProcessorType(ShaderProcessorFactory::EProcessorType::GLSL)
                               .withShaderStoragePath("Engine/Shader/GLSL")
                               .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
                               .FactoryNew<GLSLProcessor>();

    _shaderStorage = std::make_shared<ShaderStorage>(shaderProcessor);
    _shaderStorage->load(currentShader);


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
    // assume the size as we want
    _windowSize = glm::vec2((float)renderCI.swapchainCI.width, (float)renderCI.swapchainCI.height);


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
    renderTarget = new RenderTarget(renderpass);
    renderTarget->addMaterialSystem<BaseMaterialSystem>();

    _firstGraphicsQueue = &vkRender->getGraphicsQueues()[0];
    _firstPresentQueue  = &vkRender->getPresentQueues()[0];



    AssetManager::get()->loadTexture(faceTexturePath);
    AssetManager::get()->loadTexture(uv1TexturePath);

    VkSampler dummySampler = VK_NULL_HANDLE;
    vkRender->createSampler("DefaultSampler",
                            SamplerCreateInfo{
                                .minFilter     = EFilter::Linear,
                                .magFilter     = EFilter::Linear,
                                .mipmapMode    = ESamplerMipmapMode::Linear,
                                .addressModeU  = ESamplerAddressMode::Repeat,
                                .addressModeV  = ESamplerAddressMode::Repeat,
                                .addressModeW  = ESamplerAddressMode::Repeat,
                                .mipLodBias    = 0.0f,
                                .maxAnisotropy = 1.0f,
                            },
                            dummySampler);

    // we span at z = 3 and look at the origin ( right hand? and the cubes all place on xy plane )
    camera.setPosition(glm::vec3(0.0f, 0.0f, 3.0f));
    camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.setPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);



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



    loadScene(ci.defaultScenePath);
    // wait something done
    vkDeviceWaitIdle(vkRender->getLogicalDevice());
}

// MARK: QUIT
void ya::App::quit()
{
    auto *vkRender = static_cast<VulkanRender *>(_render);
    vkDeviceWaitIdle(vkRender->getLogicalDevice());

    unloadScene();


    imgui.shutdown();

    // pipeline->cleanup();
    // delete pipeline;

    // defaultPipelineLayout->cleanup();
    // delete defaultPipelineLayout;
    // descriptorSetLayout0.reset();
    // descriptorPool.reset();


    delete renderTarget;
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
        float        dtSec      = (double)dtMicroSec / 1000000.0;
        dtSec                   = std::max(dtSec, 0.0001f);
        _lastTime               = now;

        if (auto result = iterate(dtSec); result != 0) {
            break;
        }
    }

    return 0;
}


int ya::App::onEvent(SDL_Event &event)
{
    EventProcessState ret = imgui.processEvents(event);
    if (ret != EventProcessState::Continue) {
        return 0;
    }
    inputManager.processEvent(event);


#pragma region Sdl Event

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
        break;
    case SDL_EVENT_WINDOW_RESIZED:
    {
        YA_CORE_INFO("window resized {}x{}", event.window.data1, event.window.data2);
        // auto vkRender = static_cast<VulkanRender*>(_render)
        // vkRender->recreateSwapChain();
        float aspectRatio = event.window.data2 > 0 ? static_cast<float>(event.window.data1) / static_cast<float>(event.window.data2) : 1.f;
        YA_CORE_DEBUG("Window resized to {}x{}, aspectRatio: {} ", event.window.data1, event.window.data2, aspectRatio);
        camera.setAspectRatio(aspectRatio);
        _windowSize = {static_cast<float>(event.window.data1), static_cast<float>(event.window.data2)};

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
        break;
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


int ya::App::iterate(float dt)
{
    SDL_Event evt;
    SDL_PollEvent(&evt);

    if (auto result = onEvent(evt); result != 0) {
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
    camera.update(inputManager, dt); // Camera expects dt in seconds

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

    renderTarget->setColorClearValue(colorClearValue);
    renderTarget->setDepthStencilClearValue(depthClearValue);
    renderTarget->onUpdate(dt);
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
    renderTarget->begin(curCmdBuf);
    renderTarget->onRender(curCmdBuf);

#pragma region UI

    imgui.beginFrame();
    if (ImGui::Begin("Test")) {
        float fps = 1.0f / dt;
        ImGui::Text("DeltaTime: %.1f ms,\t FPS: %.1f", dt * 1000.0f, fps);
        ImGui::Text("Frame: %d, SwapchainImage: %d, FameIndex: %d", _frameIndex, currentFrameIdx, currentFrameIdx);
        static int count = 0;
        if (ImGui::Button(std::format("Click Me ({})", count).c_str())) {
            count++;
            YA_CORE_INFO("=====================================");
        }

        // ImGui::DragFloat("Texture Mix Alpha", &pushData.textureMixAlpha, 0.01, 0, 1);
        ImGui::Checkbox("Is Rotating", &bRotating);

        bool bVsync = vkRender->getSwapChain()->bVsync;
        if (ImGui::Checkbox("VSync", &bVsync)) {
            // vkRender->getSwapChain()->recreate();
            // recreate renderpass and pipeline
            // renderpass->recreate();
            // pipeline->recreate();
            // rebind frame buffer
            // renderTarget->recreate();
            taskManager.registerFrameTask([vkRender, bVsync]() {
                // TODO :bind dirty link
                vkRender->getSwapChain()->setVsync(bVsync);
            });
        }

        imcEditorCamera(camera);
        imcClearValues();
        // imcFpsControl(fpsCtrl);
        ImGui::End();
    }
    imgui.render();
    // imgui.submit(curCmdBuf, pipeline->getHandle());
    imgui.submit(curCmdBuf); // leave nullptr to let imgui use its pipeline
    imgui.endFrame();
#pragma endregion

    // TODO: subpasses?
    renderTarget->end(curCmdBuf);
    VulkanCommandPool::end(curCmdBuf);

    vkRender->end(imageIndex, {curCmdBuf});
}


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
    cubeMesh.reset();
    if (_scene)
    {
        onSceneDestroy(_scene.get());
        _scene.reset();
    }
    return true;
}

void App::onSceneInit(Scene *scene)
{
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
    cubeMesh = std::make_shared<Mesh>(vertices, indices, "cube");

    auto cube = scene->createEntity("cube1");
    {
        auto &tc = cube.addComponent<TransformComponent>();
        tc.setRotation({45, 45, 0});

        auto &mc = cube.addComponent<MeshComponent>();
        mc.mesh  = cubeMesh.get();

        cube.addComponent<BaseMaterialComponent>();
    }

    auto cube2 = scene->createEntity("cube2");
    {
        auto &tc = cube2.addComponent<TransformComponent>();
        tc.setPosition({2, 0, 0});
        tc.setScale({0.5f, 0.5f, 0.5f});

        auto &mc = cube2.addComponent<MeshComponent>();
        mc.mesh  = cubeMesh.get();

        auto &mat     = cube2.addComponent<BaseMaterialComponent>();
        mat.colorType = 0;
    }
}



} // namespace ya
