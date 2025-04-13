
// #define SDL_MAIN_USE_CALLBACKS
// #include <SDL3/SDL_main.h>

#include "SDL3/SDL_timer.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>


#include "ImGuiHelper.h"

// #include "Core/App.h"
#include "Core/EditorCamera.h"
#include "Core/Event.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Input/InputManager.h"
#include "Core/UI/DialogWindow.h"



#include "Core/AssetManager.h"
#include "Render/Model.h"
#include "Render/SDL/SDLGPUCommandBuffer.h"

#include "Render/SDL/SDLGPURender.h"
#include "Render/SDL/SDLGPURender2D.h"

SDL_GPUTexture *faceTexture;
SDL_GPUTexture *whiteTexture;

static bool bVsync = true;

// App              app;
AssetManager        assetManager;
Neon::ImguiState    imguiState;
EditorCamera        camera;
InputManager        inputManager;
SDL::GPURender_SDL *render   = new SDL::GPURender_SDL();
SDL::SDLRender2D   *render2d = new SDL::SDLRender2D();

std::queue<std::function<void()>> asyncUpdateTask;


// Current loaded model
std::shared_ptr<Model> currentModel;
bool                   useModel = false;

// Dialog window for file operations
std::unique_ptr<NeonEngine::DialogWindow> dialogWindow;

// TODO: reflect this and auto generate VertexBufferDescription and VertexAttribute
struct VertexEntry
{
    glm::vec3 position;
    glm::vec4 color;
    glm::vec2 uv;                          // aka texcoord
    glm::vec3 normal = {0.0f, 0.0f, 1.0f}; // Default normal pointing out of the screen
};
// triangle
struct IndexEntry
{
    uint32_t a, b, c;
};

// NOTICE: unifrom data member vec3 need to be aligned by vec4 bytes
struct CameraData
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

struct FragmentConstUniforms
{
    glm::vec4 lightDir         = {0.0, 0.0, -1.0, 1.0};
    glm::vec4 lightColor       = {1.0, 1.0, 1.0, 1.0};
    float     ambientIntensity = 1.f;
    float     specularPower    = 1.f;
};

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
    {0, 2, 3}, // First triangle: top-left, bottom-right, top-right (CCW)
    {0, 3, 1}, // Second triangle: top-left, bottom-left, bottom-right (CCW)
};

CameraData            cameraData;
FragmentConstUniforms fragmentUniforms;

glm::mat4 quadTransform = glm::mat4(1.0f);

struct SDLAppState
{
    void *wtf;
};


#pragma region Init


void initShaderData()
{
    auto commandBuffer = render->acquireCommandBuffer();

    for (auto &vertex : vertices) {
        vertex.position = quadTransform * glm::vec4(vertex.position, 1.0f);
    }

    // setup init vertex buffer
    commandBuffer->uploadBuffers(
        vertices.data(),
        static_cast<Uint32>(vertices.size() * sizeof(VertexEntry)),
        indices.data(),
        static_cast<Uint32>(indices.size() * sizeof(IndexEntry)));

    // Load textures using the new Texture abstraction
    std::shared_ptr<Texture> faceTextureObj = Texture::Create("Engine/Content/TestTextures/face.png", commandBuffer);

    faceTexture = faceTextureObj ? faceTextureObj->GetSDLTexture() : nullptr;

    // Create a 1x1 white texture (all pixels are white)
    const Uint32             width           = 1;
    const Uint32             height          = 1;
    const Uint8              whitePixel[4]   = {255, 255, 255, 255}; // RGBA: White with full opacity
    std::shared_ptr<Texture> whiteTextureObj = Texture::CreateFromBuffer(
        whitePixel,
        width,
        height,
        ETextureFormat::R8G8B8A8_UNORM,
        "White Texture ⬜",
        commandBuffer);
    whiteTexture = whiteTextureObj ? whiteTextureObj->GetSDLTexture() : nullptr;


    int windowWidth, windowHeight;
    SDL_GetWindowSize(render->window, &windowWidth, &windowHeight);
    NE_INFO("Initialized window size: {}x{}", windowWidth, windowHeight);
    camera.setPerspective(45.0f, (float)windowWidth / (float)windowHeight, 0.1f, 100.0f);
    camera.setPosition({0.0f, 0.0f, 5.0f});
    cameraData.model      = glm::mat4(1.0f);
    cameraData.view       = camera.getViewMatrix();
    cameraData.projection = camera.getProjectionMatrix();

    // Set up the init buffer with the camera data
    // commandBuffer->setVertexUniforms(0, &cameraData, sizeof(CameraData));
    // commandBuffer->setFragmentUniforms(0, &cameraData, sizeof(CameraData));
    // commandBuffer->setFragmentUniforms(1, &fragmentUniforms, sizeof(FragmentConstUniforms));
    commandBuffer->submit();
}

SDL_AppResult AppInit(void **appstate, int argc, char *argv[])
{

    *appstate = new SDLAppState{};

    FileSystem::init();
    Logger::init();
    AssetManager::init();

    // Create dialog window
    dialogWindow = NeonEngine::DialogWindow::create();
    if (!render->init({.bVsync = bVsync})) {
        NE_CORE_ERROR("Failed to initialize render context");
        return SDL_APP_FAILURE;
    }
    render2d->init(render->device, render->window);

    imguiState.init(render->device, render->window);

    EGraphicPipeLinePrimitiveType primitiveType = EGraphicPipeLinePrimitiveType::TriangleList;

    bool ok = render->createGraphicsPipeline(
        GraphicsPipelineCreateInfo{
            .shaderCreateInfo = {
                .shaderName = "Basic.glsl",
            },
            .primitiveType = primitiveType,
        });
    if (!ok) {
        NE_CORE_ERROR("Failed to create graphics pipeline");
        return SDL_APP_FAILURE;
    }


    initShaderData();

    return SDL_APP_CONTINUE;
}

#pragma endregion


// Function to upload model data to GPU
bool uploadModelToGPU(std::shared_ptr<Model> model, std::shared_ptr<CommandBuffer> commandBuffer)
{
    if (!model || model->getMeshes().empty()) {
        return false;
    }

    // Convert Vertex to VertexEntry
    std::vector<VertexEntry> vertexEntries;
    std::vector<uint32_t>    indexEntries;
    vertexEntries.reserve(model->getMeshes()[0].vertices.size());
    indexEntries.reserve(model->getMeshes()[0].indices.size()); // Assuming each mesh has a triangle list

    for (auto &mesh : model->getMeshes()) {
        NE_CORE_INFO("Mesh name: {}", mesh.name);


        for (const auto &vertex : mesh.vertices) {
            VertexEntry entry;
            entry.position = glm::vec4(vertex.position, 1.0f);
            entry.color    = vertex.color;
            entry.uv       = vertex.texCoord;
            entry.normal   = vertex.normal; // Assuming normal is part of the Vertex struct
            vertexEntries.push_back(entry);
        }

        indexEntries.insert(
            indexEntries.end(),
            mesh.indices.begin(),
            mesh.indices.end());
    }
    // Upload vertices and indices using the command buffer directly
    commandBuffer->uploadBuffers(
        vertexEntries.data(),
        static_cast<Uint32>(vertexEntries.size() * sizeof(VertexEntry)),
        indexEntries.data(),
        static_cast<Uint32>(indexEntries.size() * sizeof(uint32_t)));


    return true;
}

#pragma region ImGui Controls
using cmbf_t = std::shared_ptr<CommandBuffer>;

bool imcVertices(cmbf_t commandBuffer)
{
    bool bVertexInputChanged = false;
    if (ImGui::CollapsingHeader("Vertex Manipulation")) {

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
    }

    return bVertexInputChanged;
}


void imcSwapChain(cmbf_t commandBuffer)
{
    auto d = render->device;
    auto w = render->window;

    // TODO: abstract this to a function in NeonEngine::Render
    NE_ASSERT(SDL_WindowSupportsGPUSwapchainComposition(d, w, SDL_GPU_SWAPCHAINCOMPOSITION_SDR),
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
        SDL_SetGPUSwapchainParameters(d,
                                      w,
                                      SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                      currentPresentMode);
        NE_CORE_INFO("Changed presentation mode to: {}", presentModes[currentPresentMode]);
    }
}


void imcLight(cmbf_t commandBuffer)
{
    if (!ImGui::CollapsingHeader("Light Controls")) {
        return;
    }

    bool bChanged = false;


    if (ImGui::DragFloat3("Light Direction", glm::value_ptr(fragmentUniforms.lightDir), 0.03f, -90.0f, 90.0f)) {
        bChanged = true;
    }
    if (ImGui::ColorEdit3("Light Color", glm::value_ptr(fragmentUniforms.lightColor))) {
        bChanged = true;
    }
    if (ImGui::DragFloat("Ambient Intensity", &fragmentUniforms.ambientIntensity, 0.01f, 0.0f, 1.0f)) {
        bChanged = true;
    }
    if (ImGui::DragFloat("Specular Power", &fragmentUniforms.specularPower, 0.001f, 0.0f, 1.0f)) {
        bChanged = true;
    }
    if (bChanged) {
        // commandBuffer->setFragmentUniforms(1, &fragmentUniforms, sizeof(FragmentConstUniforms));
    }
}



bool imcEditorCamera(cmbf_t commandBuffer)
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


void imcModel(cmbf_t commandBuffer)
{
    if (!ImGui::CollapsingHeader("Model Controls")) {
        return;
    }

    // TODO: why copilot think this is wrong? must a char[256] with '\0' at the end?
    static std::string modelPath(256, '\0');
    ImGui::InputText("Model Path", modelPath.data(), modelPath.size());

    if (ImGui::Button("Browse..."))
    {
        // Create dialog window if it doesn't exist yet
        if (!dialogWindow) {
            dialogWindow = NeonEngine::DialogWindow::create();
        }

        if (dialogWindow) {
            // Define file filters for 3D models
            std::vector<std::pair<std::string, std::string>> filters = {
                {"3D Models", "*.obj;*.fbx;*.gltf;*.glb"},
                {"Wavefront OBJ", "*.obj"},
                {"Autodesk FBX", "*.fbx"},
                {"GLTF", "*.gltf;*.glb"},
                {"All Files", "*.*"}};

            auto result = dialogWindow->showDialog(
                NeonEngine::DialogType::OpenFile,
                "Select 3D Model",
                filters);

            if (result.has_value()) {
                // Copy the path to the input field, ensuring it doesn't overflow
                modelPath = result.value();
                modelPath.push_back('\0'); // Null-terminate the string
                NE_CORE_INFO("Selected model file: {}", modelPath);
            }
        }
    }

    if (ImGui::Button("Load Model")) {
        std::shared_ptr<Model> model = assetManager.loadModel(modelPath, commandBuffer);

        if (model) {
            currentModel = model;
            useModel     = true;

            // Upload model data
            if (uploadModelToGPU(model, commandBuffer)) {
                NE_CORE_INFO("Model loaded and uploaded successfully");
            }
            else {
                NE_CORE_ERROR("Failed to upload model data");
            }
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Use Quad")) {
        useModel = false;
        commandBuffer->uploadBuffers(
            vertices.data(),
            static_cast<Uint32>(vertices.size() * sizeof(VertexEntry)),
            indices.data(),
            static_cast<Uint32>(indices.size() * sizeof(IndexEntry)));
    }

    // Model transform controls
    if (useModel && currentModel) {
        ImGui::Separator();
        ImGui::Text("Model Transform");

        static glm::vec3 position = {0.0f, 0.0f, 0.0f};
        static glm::vec3 rotation = {0.0f, 0.0f, 0.0f};
        static glm::vec3 scale    = {1.0f, 1.0f, 1.0f};

        bool transformChanged = false;

        if (ImGui::DragFloat3("Position", glm::value_ptr(position), 0.01f)) {
            transformChanged = true;
        }

        if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotation), 1.0f)) {
            transformChanged = true;
        }

        if (ImGui::DragFloat3("Scale", glm::value_ptr(scale), 0.01f, 0.01f, 10.0f)) {
            transformChanged = true;
        }

        if (transformChanged) {
            glm::mat4 transform = glm::mat4(1.0f);
            transform           = glm::translate(transform, position);
            transform           = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            transform           = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            transform           = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            transform           = glm::scale(transform, scale);

            currentModel->setTransform(transform);

            cameraData.model = transform;
            commandBuffer->setVertexUniforms(0, &cameraData, sizeof(CameraData));
        }
    }
}


#pragma endregion

SDL_AppResult AppIterate(void *appState)
{

#pragma region Update
    // if (app.bPausing) {
    //     return SDL_APP_CONTINUE;
    // }

    // Calculate delta time and FPS
    static Uint64 lastTime    = SDL_GetTicks();
    Uint64        currentTime = SDL_GetTicks();
    float         deltaTime   = (currentTime - lastTime) / 1000.0f;
    float         fps         = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;
    lastTime                  = currentTime;

    // Smooth FPS display
    static float avgFps = 0.0f;
    avgFps              = avgFps * 0.95f + fps * 0.05f; // Simple exponential moving average


    inputManager.update();
    camera.update(inputManager, deltaTime);


    if (SDL_GetWindowFlags(render->window) & SDL_WINDOW_MINIMIZED)
    {
        SDL_Delay(100);
        return SDL_APP_CONTINUE;
    }

    while (!asyncUpdateTask.empty()) {
        auto task = asyncUpdateTask.front();
        asyncUpdateTask.pop();
        task();
    }

#pragma endregion

#pragma region Render

    std::shared_ptr<CommandBuffer> commandBuffer = render->acquireCommandBuffer();
    if (!commandBuffer) {
        NE_CORE_ERROR("Failed to acquire command buffer {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    auto sdlCommandBuffer = static_cast<SDL::GPUCommandBuffer_SDL *>(commandBuffer.get());

    Uint32          swapChainTextureWidth, swapChainTextureHeight;
    SDL_GPUTexture *swapchainTexture = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture((sdlCommandBuffer->commandBuffer),
                                               render->window,
                                               &swapchainTexture,
                                               &swapChainTextureWidth,
                                               &swapChainTextureHeight)) {
        NE_CORE_ERROR("Failed to acquire swapchain texture {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    // when window is minimized, the swapchainTexture will be null
    if (!swapchainTexture) {
        return SDL_APP_CONTINUE;
    }

    static glm::vec4    clearColor      = {0.0f, 0.0f, 0.0f, 1.0f};
    static ESamplerType selectedSampler = ESamplerType::PointClamp;

    bool      bVertexInputChanged = false;
    bool      bCameraChanged      = false;
    glm::mat4 transform           = glm::mat4(1.0f); // current rectangle has no position data
    bool      bImguiMinimized     = false;

    imguiState.beginFrame();
    // UI code remains in Entry.cpp
    if (ImGui::Begin("Debug"))
    {
        // Display FPS at the top of the debug window
        ImGui::Text("FPS: %.1f (%.3f ms/frame)", avgFps, 1000.0f / (avgFps > 0 ? avgFps : 1.0f));
        ImGui::Separator();
        if (ImGui::Checkbox("Vsync", &bVsync)) {

            asyncUpdateTask.emplace([bVsync = bVsync]() {
                SDL_SetGPUSwapchainParameters(render->device,
                                              render->window,
                                              SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                              bVsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE);
            });
        }

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

        bVertexInputChanged = imcVertices(commandBuffer);
        bCameraChanged      = imcEditorCamera(commandBuffer);


        imcModel(commandBuffer);
        imcSwapChain(commandBuffer);
        imcLight(commandBuffer);
    }
    ImGui::End();
    bImguiMinimized = imguiState.render(sdlCommandBuffer->commandBuffer);
    imguiState.prepareDrawData(sdlCommandBuffer->commandBuffer);

    render2d->beginFrame(sdlCommandBuffer->commandBuffer, camera);
    render2d->drawQuad(
        {0.0f, 0.0f},
        0,
        {1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f});
    render2d->submit();

#pragma endregion

#pragma region Draw
    if (!swapchainTexture || bImguiMinimized)
    {
        if (!commandBuffer->submit()) {
            NE_CORE_ERROR("Failed to submit command buffer {}", SDL_GetError());
        }
        return SDL_APP_CONTINUE;
    }


    // Unifrom buffer should be update continuously, or we can use a ring buffer to store the data
    cameraData.view       = camera.getViewMatrix();
    cameraData.projection = camera.getProjectionMatrix();

    // dose all unifroms need to be update each frame?
    commandBuffer->setVertexUniforms(0, &cameraData, sizeof(CameraData));
    commandBuffer->setFragmentUniforms(0, &cameraData, sizeof(CameraData));
    commandBuffer->setFragmentUniforms(1, &fragmentUniforms, sizeof(FragmentConstUniforms));

    if (bVertexInputChanged) {
        // TODO: move to render pipeline
        NE_CORE_INFO("Vertex input changed, update vertex buffer");
        std::vector<VertexEntry> verticesCopy = vertices;
        for (auto &vertex : verticesCopy) {
            vertex.position = quadTransform * glm::vec4(vertex.position, 1.0f);
        }

        commandBuffer->uploadVertexBuffers(
            verticesCopy.data(),
            static_cast<Uint32>(verticesCopy.size() * sizeof(VertexEntry)));
    }


    // target info can be multiple(use same pipeline?)
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

    SDL_GPURenderPass *renderpass = SDL_BeginGPURenderPass(sdlCommandBuffer->commandBuffer,
                                                           &colorTargetInfo,
                                                           1,
                                                           nullptr);
    {
        SDL_BindGPUGraphicsPipeline(renderpass, render->pipeline);

        SDL_GPUBufferBinding vertexBufferBinding = {
            .buffer = render->vertexBuffer,
            .offset = 0,
        };
        SDL_BindGPUVertexBuffers(renderpass, 0, &vertexBufferBinding, 1);

        // TODO: use uint16  to optimize index buffer
        SDL_GPUBufferBinding indexBufferBinding = {
            .buffer = render->indexBuffer,
            .offset = 0,
        };
        SDL_BindGPUIndexBuffer(renderpass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        // Determine which texture to use
        SDL_GPUTexture *textureToUse = whiteTexture;
        if (useModel && currentModel && !currentModel->getMeshes().empty()) {
            const auto &firstMesh = currentModel->getMeshes()[0];
            if (firstMesh.diffuseTexture) {
                textureToUse = firstMesh.diffuseTexture->GetSDLTexture();
            }
        }

        // sampler binding
        SDL_GPUTextureSamplerBinding textureBinding = {
            .texture = textureToUse,
            .sampler = render->samplers[selectedSampler],
        };
        SDL_BindGPUFragmentSamplers(renderpass, 0, &textureBinding, 1);

        int windowWidth, windowHeight;
        SDL_GetWindowSize(render->window, &windowWidth, &windowHeight);

        SDL_GPUViewport viewport = {
            .x         = 0,
            .y         = 0,
            .w         = static_cast<float>(windowWidth),
            .h         = static_cast<float>(windowHeight),
            .min_depth = 0.0f,
            .max_depth = 1.0f,
        };
        SDL_SetGPUViewport(renderpass, &viewport);

        // Draw the model or quad
        if (useModel && currentModel && !currentModel->getMeshes().empty()) {
            // Draw the model
            const auto &firstMesh = currentModel->getMeshes()[0];
            SDL_DrawGPUIndexedPrimitives(renderpass,
                                         static_cast<uint32_t>(firstMesh.indices.size()),
                                         1,
                                         0,
                                         0,
                                         0);
        }
        else {
            // Draw the quad
            SDL_DrawGPUIndexedPrimitives(renderpass,
                                         2 * 3, // 3 index for each triangle
                                         1,
                                         0,
                                         0,
                                         0);
        }

        // after graphics pipeline draw, or make pipeline draw into a RT
        imguiState.draw(sdlCommandBuffer->commandBuffer, renderpass);
    }

    render2d->render(renderpass);

    SDL_EndGPURenderPass(renderpass);

    if (!commandBuffer->submit()) {
        NE_CORE_ERROR("Failed to submit command buffer {}", SDL_GetError());
    }

#pragma endregion

    return SDL_APP_CONTINUE;
}

SDL_AppResult AppEvent(void *appstate, SDL_Event *evt)
{
    // NE_CORE_TRACE("Event: {}", event->type);
    EventProcessState state = EventProcessState::Continue;

    // TODO: priority of each event handler?
    if (state = imguiState.processEvents(*evt); state) {
        NE_CORE_WARN("Imgui handled event: {}", evt->type);
        return SDL_APP_CONTINUE;
    }
    if (state = inputManager.processEvent(*evt); state) {
        NE_CORE_WARN("InputManager handled event: {}", evt->type);
        return SDL_APP_CONTINUE;
    }

    switch ((SDL_EventType)evt->type) {
    case SDL_EventType::SDL_EVENT_KEY_UP:
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Key up: %d", evt->key.key);
        // get all  modifiers to bool
        bool bShift = (evt->key.mod & SDL_KMOD_SHIFT) != 0;
        bool bCtrl  = (evt->key.mod & SDL_KMOD_CTRL) != 0;
        bool bAlt   = (evt->key.mod & SDL_KMOD_ALT) != 0;
        if (bShift && evt->key.key == SDLK_ESCAPE)
        {
            return SDL_APP_SUCCESS;
        }
    } break;
    case SDL_EventType::SDL_EVENT_WINDOW_RESIZED:
    {
        if (evt->window.windowID == SDL_GetWindowID(render->window))
        {
            SDL_WaitForGPUIdle(render->device);
            NE_CORE_INFO("Window resized to {}x{}", evt->window.data1, evt->window.data2);
            camera.setAspectRatio(static_cast<float>(evt->window.data1) / static_cast<float>(evt->window.data2));
        }
    } break;
    case SDL_EventType::SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    {
        NE_CORE_INFO("SDL Window Close Requested {}", evt->window.windowID);
        if (evt->window.windowID == SDL_GetWindowID(render->window))
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

void AppQuit(void *appstate, SDL_AppResult result)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "sdl quit with result: %u", result);

    SDL_WaitForGPUIdle(render->device);

    imguiState.shutdown();

    if (faceTexture) {
        SDL_ReleaseGPUTexture(render->device, faceTexture);
    }
    if (whiteTexture) {
        SDL_ReleaseGPUTexture(render->device, whiteTexture);
    }

    // Clean up dialog window
    dialogWindow.reset();

    render->clean();

    auto sdlAppState = static_cast<SDLAppState *>(appstate);
    delete sdlAppState;
    SDL_Quit();
}


#pragma region Entry

int main()
{
    void **appState = new void *();

    SDL_AppResult result = AppInit(appState, 0, nullptr);
    if (result != SDL_APP_CONTINUE) {
        NE_CORE_ERROR("SDL App exited with error: {}", (int)result);
    }

    while (result == SDL_APP_CONTINUE) {
        SDL_Event evt;
        SDL_PollEvent(&evt);
        if (result = AppEvent(*appState, &evt); result != SDL_APP_CONTINUE) {
            break;
        }
        if (result = AppIterate(nullptr); result != SDL_APP_CONTINUE) {
            break;
        }
    }

    AppQuit(*appState, result);

    return result == SDL_APP_SUCCESS ? 0 : result;
}
#pragma endregion