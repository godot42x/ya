#include "App.h"
#include "Core/FName.h"
#include "Core/FileSystem/FileSystem.h"

#include "Platform/Render/Vulkan/VulkanRender.h"

#include "Render/Render.h"
#include "Render/Render2DIntegration.h"

#include <SDL3/SDL_events.h>

namespace Neon
{


App *App::_instance = nullptr;

App *App::create()
{
    NE_CORE_ASSERT(App::_instance == nullptr, "App instance already exists!");
    App::_instance = new App();
    return App::_instance;
}

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

    vkRender = new VulkanRender();

    IRender::InitParams params = {
        .bVsync         = true,
        .renderAPI      = ERenderAPI::Vulkan,
        .windowProvider = windowProvider,
        .swapchainCI    = SwapchainCreateInfo{
               .bVsync = true,
               .width  = static_cast<uint32_t>(w),
               .height = static_cast<uint32_t>(h),
        },
        .renderPassCI = RenderPassCreateInfo{
            .attachments = {
                // color
                AttachmentDescription{
                    .format                  = EFormat::B8G8R8A8_UNORM,
                    .loadOp                  = EAttachmentLoadOp::Clear,
                    .storeOp                 = EAttachmentStoreOp::Store,
                    .bInitialLayoutUndefined = true,
                },
                // depth
                AttachmentDescription{
                    .format                  = EFormat::D32_SFLOAT,
                    .loadOp                  = EAttachmentLoadOp::Clear,
                    .storeOp                 = EAttachmentStoreOp::DontCare,
                    .bInitialLayoutUndefined = true,
                },
            },
            .dependencies = {
                RenderPassCreateInfo::SubpassDependency{
                    .bSrcExternal = true, // External source
                    .dstSubpass   = 0,
                },
            },
            .subpasses = {
                RenderPassCreateInfo::SubpassInfo{
                    .colorAttachmentIndices      = {0},
                    .depthStencilAttachmentIndex = 1,
                    .inputAttachmentIndices      = {1},
                },
            },
            .pipelineCIs = {
                GraphicsPipelineCreateInfo{
                    .shaderCreateInfo = ShaderCreateInfo{
                        .shaderName = "SimpleTriangle.glsl",
                    },
                    // could be defined by shader
                    .vertexAttributes = {
                        VertexAttribute{
                            .location   = 0,
                            .bufferSlot = 0,
                            .format     = EVertexAttributeFormat::Float3,
                            .offset     = 0,
                        },
                        VertexAttribute{
                            .location   = 1,
                            .bufferSlot = 0,
                            .format     = EVertexAttributeFormat::Float2,
                            .offset     = sizeof(float) * 3,
                        },
                    },
                    .rasterizationState = RasterizationState{
                        .bDepthClampEnable = false,
                        .polygonMode       = EPolygonMode::Fill,
                        .cullMode          = ECullMode::Back,
                    },
                    .colorBlendState = ColorBlendState{
                        .bLogicOpEnable = false,
                        .logicOp        = ELogicOp::NoOp,
                        .attachments    = {
                            // ColorBlendAttachment{
                            //        .blendEnable   = true,
                            //        .srcColorBlend = EBlendFactor::SrcAlpha,
                            //        .dstColorBlend = EBlendFactor::OneMinusSrcAlpha,
                            //        .colorBlendOp  = EBlendOp::Add,
                            //        .srcAlphaBlend = EBlendFactor::One,
                            //        .dstAlphaBlend = EBlendFactor::Zero,
                            //        .alphaBlendOp  = EBlendOp::Add,
                            // },
                        },
                    },
                },
            },
        }};

    vkRender->init(params);
    
    // Initialize 2D rendering system after Vulkan renderer
    if (!Neon::Render2DIntegration::initialize()) {
        NE_CORE_ERROR("Failed to initialize 2D renderer");
    }
    
    // Set screen size for 2D rendering
    Neon::Render2DIntegration::setScreenSize(static_cast<float>(w), static_cast<float>(h));
}

void Neon::App::quit()
{
    // Shutdown 2D renderer before main renderer
    Neon::Render2DIntegration::shutdown();
    
    vkRender->destroy();
    windowProvider->destroy();

    delete vkRender;
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
    
    // Begin 2D rendering frame
    Neon::Render2DIntegration::beginFrame();
    
    // Render example 2D UI
    Neon::Render2DIntegration::renderExampleUI();
    
    // End 2D rendering frame
    Neon::Render2DIntegration::endFrame();

#if USE_VULKAN
    auto render = static_cast<VulkanRender *>(vkRender);
    render->DrawFrame(); // Draw triangle!
    render->OnPostUpdate();

#endif
    // Handle input, update logic, render, etc.
    // This is where the main loop logic would go.
    return 0; // Continue running
}

} // namespace Neon