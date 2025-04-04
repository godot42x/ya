#include <array>
#include <cmath>
#include <optional>


#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>


#include "Core/App.h"
#include "Core/FileSystem/FileSystem.h"
#include "Render/Render.h"
#include "Render/Shader.h"

#include "Core/EditorCamera.h"
#include "Core/Input/InputManager.h"


SDL_GPUTexture *faceTexture;
SDL_GPUTexture *whiteTexture;

App          app;
SDLGPURender render;
EditorCamera camera;
InputManager inputManager;



// TODO: reflect this and auto generate VertexBufferDescription and VertexAttribute
struct VertexEntry
{
    glm::vec3 position;
    glm::vec4 color;
    glm::vec2 uv; // aka texcoord
};
// triangle
struct IndexEntry
{
    uint32_t a, b, c;
};

struct CameraData
{
    glm::mat4 viewProjectionMatrix;
};

// quad vertices
std::vector<VertexEntry> vertices = {
    // lt
    VertexEntry{
        {-0.5f, 0.5f, 0.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f},
    },
    // rt
    VertexEntry{
        {0.5f, 0.5f, 0.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 0.0f},
    },
    // lb
    VertexEntry{
        .position = {-0.5, -0.5f, 0.f},
        .color    = {1.0f, 1.0f, 1.0f, 1.0f},
        .uv       = {0.0f, 1.0f},
    },
    // rb
    VertexEntry{
        .position = {0.5f, -0.5f, 0.0f},
        .color    = {1.0f, 1.0f, 1.0f, 1.0f},
        .uv       = {1.0f, 1.0f},
    },
};
// quad indices
std::vector<IndexEntry> indices = {
    {0, 1, 3},
    {0, 3, 2},
};


CameraData cameraData;

glm::mat4 quadTransform = glm::mat4(1.0f);

void initImGui(SDL_GPUDevice *device, SDL_Window *window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO *io = &ImGui::GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo info{
        .Device            = device,
        .ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window),
        .MSAASamples       = SDL_GPU_SAMPLECOUNT_1,
    };
    ImGui_ImplSDLGPU3_Init(&info);
}



SDLMAIN_DECLSPEC SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    FileSystem::init();
    Logger::init();

    if (!render.init()) {
        NE_CORE_ERROR("Failed to initialize render context");
        return SDL_APP_FAILURE;
    }

    ::initImGui(render.device, render.window);

    EGraphicPipeLinePrimitiveType primitiveType = EGraphicPipeLinePrimitiveType::TriangleList;


    bool ok = render.createGraphicsPipeline(
        GraphicsPipelineCreateInfo{
            .shaderCreateInfo = {
                .shaderName        = "Test.glsl",
                .numUniformBuffers = 1,
                .numSamplers       = 1,
            },
            .vertexBufferDescs = {
                {
                    0,
                    sizeof(VertexEntry),
                },
            },
            .vertexAttributes = {
                {
                    0,
                    0,
                    EVertexAttributeFormat::Float3,
                    offsetof(VertexEntry, position),

                },
                {
                    1,
                    0,
                    EVertexAttributeFormat::Float4,
                    offsetof(VertexEntry, color),
                },
                {
                    2,
                    0,
                    EVertexAttributeFormat::Float2,
                    offsetof(VertexEntry, uv),
                },
            },
            .primitiveType = primitiveType,
        });
    if (!ok) {
        NE_CORE_ERROR("Failed to create graphics pipeline");
        return SDL_APP_FAILURE;
    }


    auto commandBuffer = render.acquireCommandBuffer();

    for (auto &vertex : vertices) {
        vertex.position = quadTransform * glm::vec4(vertex.position, 1.0f);
    }

    render.uploadVertexBuffers(
        commandBuffer,
        vertices.data(),
        static_cast<Uint32>(vertices.size() * sizeof(VertexEntry)));

    faceTexture = render.createTexture(commandBuffer, "Engine/Content/TestTextures/face.png");

    // Create a 1x1 white texture (all pixels are white)
    const Uint32 width         = 1;
    const Uint32 height        = 1;
    const Uint8  whitePixel[4] = {255, 255, 255, 255}; // RGBA: White with full opacity
    whiteTexture               = render.createTextureByBuffer(commandBuffer, whitePixel, width, height, "White Texture ⬜");


    camera.setPerspective(45.0f, 1.0f, 0.1f, 100.0f);
    cameraData.viewProjectionMatrix = camera.getViewProjectionMatrix();
    render.setVertexUnifroms(commandBuffer, 0, &cameraData, sizeof(CameraData));

    commandBuffer->submit();


    return SDL_APP_CONTINUE;
}


bool imguiManipulateVertices()
{

    bool bVertexInputChanged = false;
    for (int i = 0; i < 4; ++i) {
        ImGui::Text("Vertex %d", i);
        char label[32];
        sprintf(label, "position##%d", i);
        if (ImGui::DragFloat3(label, glm::value_ptr(vertices[i].position))) {
            bVertexInputChanged = true;
        }
        sprintf(label, "color##%d", i);
        if (ImGui::DragFloat4(label, glm::value_ptr(vertices[i].color))) {
            bVertexInputChanged = true;
        }
        sprintf(label, "uv##%d", i);
        if (ImGui::DragFloat2(label, glm::value_ptr(vertices[i].uv))) {
            bVertexInputChanged = true;
        }
    }

    return bVertexInputChanged;
}



void imguiManipulateSwapchain()
{

    NE_ASSERT(SDL_WindowSupportsGPUSwapchainComposition(render.device, render.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR),
              "Window does not support GPU swapchain composition");


    static SDL_GPUPresentMode currentPresentMode = SDL_GPU_PRESENTMODE_VSYNC;

    ImGui::Separator();
    const char *presentModes[] = {
        "Vsync",
        "Immediate",
        "Mailbox",
    };

    if (ImGui::Combo("Present Mode", (int *)&currentPresentMode, presentModes, IM_ARRAYSIZE(presentModes)))
    {

        SDL_SetGPUSwapchainParameters(render.device,
                                      render.window,
                                      SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                      currentPresentMode);
        NE_CORE_INFO("Changed presentation mode to: {}", presentModes[currentPresentMode]);
    }
}

bool imguiManipulateEditorCamera()
{
    auto position = camera.position;
    auto rotation = camera.rotation;
    bool bChanged = false;

    if (ImGui::DragFloat3("Camera Position", glm::value_ptr(position), 0.01f, -100.0f, 100.0f)) {
        bChanged = true;
    }
    if (ImGui::DragFloat3("Camera Rotation", glm::value_ptr(rotation), 1.f, -180.0f, 180.0f)) {
        bChanged = true;
    }

    if (bChanged) {
        camera.setPositionAndRotation(position, rotation);
    }
    return bChanged;
}


SDL_AppResult iterate()
{
    if (app.bPausing) {
        return SDL_APP_CONTINUE;
    }

    // Calculate delta time and FPS
    static Uint64 lastTime    = SDL_GetTicks();
    Uint64        currentTime = SDL_GetTicks();
    float         deltaTime   = (currentTime - lastTime) / 1000.0f;
    float         fps         = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;
    lastTime                  = currentTime;

    // Smooth FPS display
    static float avgFps = 0.0f;
    avgFps              = avgFps * 0.95f + fps * 0.05f; // Simple exponential moving average

    // Update input manager
    inputManager.update();

    // Update camera based on input
    camera.update(inputManager, deltaTime);

    if (SDL_GetWindowFlags(render.window) & SDL_WINDOW_MINIMIZED)
    {
        SDL_Delay(100);
        return SDL_APP_CONTINUE;
    }

    std::shared_ptr<GPUCommandBuffer> commandBuffer = render.acquireCommandBuffer();
    if (!commandBuffer) {
        NE_CORE_ERROR("Failed to acquire command buffer {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    Uint32          swapChianTextureW, swapChainTextureHeight;
    SDL_GPUTexture *swapchainTexture = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(*commandBuffer,
                                               render.window,
                                               &swapchainTexture,
                                               &swapChianTextureW,
                                               &swapChainTextureHeight)) {
        NE_CORE_ERROR("Failed to acquire swapchain texture {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    // when window is minimized, the swapchainTexture will be null
    if (!swapchainTexture) {
        return SDL_APP_CONTINUE;
    }

    static glm::vec4 clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    bool                bVertexInputChanged = false;
    bool                bCameraChanged      = false;
    static ESamplerType selectedSampler     = ESamplerType::PointClamp;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (ImGui::Begin("Debug"))
    {
        // Display FPS at the top of the debug window
        ImGui::Text("FPS: %.1f (%.3f ms/frame)", avgFps, 1000.0f / (avgFps > 0 ? avgFps : 1.0f));
        ImGui::Separator();

        ImGui::DragFloat4("Clear Color", glm::value_ptr(clearColor), 0.01f, 0.0f, 1.0f);

        const std::string currentSamplerName = ESamplerType2Strings[selectedSampler];
        if (ImGui::BeginCombo("Sampler", currentSamplerName.c_str()))
        {
            for (int i = 0; i < static_cast<int>(ESamplerType::ENUM_MAX); i++)
            {
                bool              bSelected   = (static_cast<int>(selectedSampler) == i);
                const std::string samplerName = ESamplerType2Strings[static_cast<ESamplerType>(i)];
                if (ImGui::Selectable(samplerName.c_str(), &bSelected)) {
                    selectedSampler = static_cast<ESamplerType>(i);
                    NE_CORE_INFO("Selected sampler: {}", samplerName);
                }
                if (bSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        bVertexInputChanged = imguiManipulateVertices();

        // Camera UI still useful for manual positioning
        if (imguiManipulateEditorCamera()) {
            // Manual camera manipulation from UI overrides input-based camera position
            bCameraChanged = true;
        }

        // Add camera control settings to UI
        if (ImGui::CollapsingHeader("Camera Controls")) {
            ImGui::DragFloat("Move Speed", &camera.moveSpeed, 0.1f, 0.1f, 20.0f);
            ImGui::DragFloat("Rotation Speed", &camera.rotationSpeed, 0.01f, 0.01f, 1.0f);
            ImGui::Text("Hold right mouse button to rotate camera");
            ImGui::Text("WASD: Move horizontally, QE: Move vertically");
        }

        imguiManipulateSwapchain();
    }
    ImGui::End();
    ImGui::Render();
    ImDrawData *drawData   = ImGui::GetDrawData();
    const bool  bMinimized = drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f;


    // current rectangle has no position data
    glm::mat4 transform = glm::mat4(1.0f);


    if (swapchainTexture && !bMinimized)
    {
        Imgui_ImplSDLGPU3_PrepareDrawData(drawData, *commandBuffer);


        SDL_GPUColorTargetInfo colorTargetInfo = {
            .texture               = swapchainTexture,
            .mip_level             = 0,
            .layer_or_depth_plane  = 0,
            .clear_color           = {clearColor.r, clearColor.g, clearColor.b, clearColor.a},
            .load_op               = SDL_GPU_LOADOP_CLEAR,
            .store_op              = SDL_GPU_STOREOP_STORE,
            .cycle                 = true, // wtf?
            .cycle_resolve_texture = false,
        };

        // Unifrom buffer should be update continuously, or we can use a ring buffer to store the data
        // if (bCameraChanged) {
        // NE_CORE_INFO("Camera changed, update view projection matrix");
        cameraData.viewProjectionMatrix = camera.getViewProjectionMatrix();
        render.setVertexUnifroms(commandBuffer, 0, &cameraData, sizeof(CameraData));
        // }

        if (bVertexInputChanged) {

            // TODO: move to render pipeline
            NE_CORE_INFO("Vertex input changed, update vertex buffer");
            std::vector<VertexEntry> verticesCopy = vertices;
            for (auto &vertex : verticesCopy) {
                vertex.position = quadTransform * glm::vec4(vertex.position, 1.0f);
            }

            render.uploadVertexBuffers(
                commandBuffer,
                verticesCopy.data(),
                static_cast<Uint32>(verticesCopy.size() * sizeof(VertexEntry)));
        }

        // target info can be multiple(use same pipeline?)
        SDL_GPURenderPass *renderpass = SDL_BeginGPURenderPass(*commandBuffer,
                                                               &colorTargetInfo,
                                                               1,
                                                               nullptr);
        {
            SDL_BindGPUGraphicsPipeline(renderpass, render.pipeline);

            SDL_GPUBufferBinding vertexBufferBinding = {
                .buffer = render.vertexBuffer,
                .offset = 0,
            };
            SDL_BindGPUVertexBuffers(renderpass, 0, &vertexBufferBinding, 1);

            // TODO: use uint16  to optimize index buffer
            SDL_GPUBufferBinding indexBufferBinding = {
                .buffer = render.indexBuffer,
                .offset = 0,
            };
            SDL_BindGPUIndexBuffer(renderpass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

            // sampler binding
            SDL_GPUTextureSamplerBinding textureBinding = {
                .texture = faceTexture,
                .sampler = render.samplers[selectedSampler],
            };
            SDL_BindGPUFragmentSamplers(renderpass, 0, &textureBinding, 1);

            int windowWidth, windowHeight;
            SDL_GetWindowSize(render.window, &windowWidth, &windowHeight);

            // TODO: these works should be done in camera matrix?
            // Calculate proper aspect-preserving dimensions
            // float targetAspect = 1.0f; // 1:1 for square
            // float windowAspect = (float)windowWidth / (float)windowHeight;
            // float viewportWidth , viewportHeight;
            // float offsetX = 0;
            // float offsetY = 0;

            // if (windowAspect > targetAspect) {
            //     // Window is wider than needed
            //     viewportHeight = (float)windowHeight;
            //     viewportWidth  = viewportHeight * targetAspect;
            //     offsetX        = (windowWidth - viewportWidth) / 2.0f;
            // }
            // else {
            //     // Window is taller than needed
            //     viewportWidth  = (float)windowWidth;
            //     viewportHeight = viewportWidth / targetAspect;
            //     offsetY        = (windowHeight - viewportHeight) / 2.0f;
            // }

            SDL_GPUViewport viewport = {
                .x         = 0,
                .y         = 0,
                .w         = static_cast<float>(windowWidth),
                .h         = static_cast<float>(windowHeight),
                .min_depth = 0.0f,
                .max_depth = 1.0f,
            };
            SDL_SetGPUViewport(renderpass, &viewport);

            // SDL_BindGPUVertexBuffer(renderpass, 0, vertexBuffer, 0);
            // SDL_DrawGPUPrimitives(renderpass, 3, 1, 0, 0);
            SDL_DrawGPUIndexedPrimitives(renderpass,
                                         2 * 3, // 3 index for each triangle
                                         1,
                                         0,
                                         0,
                                         0);

            // after graphics pipeline draw, or make pipeline draw into a RT
            if (drawData && drawData->CmdListsCount > 0) {
                ImGui_ImplSDLGPU3_RenderDrawData(drawData, *commandBuffer, renderpass);
            }
        }
        SDL_EndGPURenderPass(renderpass);
    }

    if (!commandBuffer->submit()) {
        NE_CORE_ERROR("Failed to submit command buffer {}", SDL_GetError());
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    try {
        return iterate();
    }
    catch (const std::exception &e) {
        NE_CORE_ERROR("Exception: {}", e.what());
        return SDL_APP_FAILURE;
    }
    catch (...) {
        NE_CORE_ERROR("Unknown exception");
        return SDL_APP_FAILURE;
    }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    // NE_CORE_TRACE("Event: {}", event->type);

    ImGui_ImplSDL3_ProcessEvent(event);
    inputManager.processEvent(*event);

    switch ((SDL_EventType)event->type) {
    case SDL_EventType::SDL_EVENT_KEY_UP:
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Key up: %d", event->key.key);
        // get all  modifiers to bool
        bool bShift = (event->key.mod & SDL_KMOD_SHIFT) != 0;
        bool bCtrl  = (event->key.mod & SDL_KMOD_CTRL) != 0;
        bool bAlt   = (event->key.mod & SDL_KMOD_ALT) != 0;
        if (bShift && event->key.key == SDLK_ESCAPE)
        {
            return SDL_APP_SUCCESS;
        }
    } break;
    case SDL_EventType::SDL_EVENT_WINDOW_RESIZED:
    {
        if (event->window.windowID == SDL_GetWindowID(render.window))
        {
            SDL_WaitForGPUIdle(render.device);
            NE_CORE_INFO("Window resized to {}x{}", event->window.data1, event->window.data2);
        }
    } break;
    case SDL_EventType::SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    {
        NE_CORE_INFO("SDL Window Close Requested {}", event->window.windowID);
        if (event->window.windowID == SDL_GetWindowID(render.window))
        {
            return SDL_APP_SUCCESS;
        }
    } break;
    case SDL_EventType::SDL_EVENT_QUIT:
    {
        NE_CORE_INFO("SDL Quit");
        return SDL_APP_SUCCESS;
    } break;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "sdl quit with result: %u", result);

    SDL_WaitForGPUIdle(render.device);

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();

    if (faceTexture) {
        SDL_ReleaseGPUTexture(render.device, faceTexture);
    }
    if (whiteTexture) {
        SDL_ReleaseGPUTexture(render.device, whiteTexture);
    }

    render.cleanContext();

    SDL_Quit();
}
