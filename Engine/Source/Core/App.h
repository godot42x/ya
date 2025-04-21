#pragma once

#include "SDL3/SDL.h"
#include "SDL3/SDL_timer.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <string>
#include <vector>

#include "../ImGuiHelper.h"
#include "../Render/Model.h"
#include "../Render/SDL/SDLGPUCommandBuffer.h"
#include "../Render/SDL/SDLGPURender.h"
#include "AssetManager.h"
#include "EditorCamera.h"
#include "Input/InputManager.h"
#include "UI/DialogWindow.h"


namespace NeonEngine
{

class App
{
    // Vertices and indices for the quad
    struct VertexEntry
    {
        glm::vec3 position;
        glm::vec4 color;
        glm::vec2 uv; // aka texcoord
    };
    struct IndexEntry
    {
        uint32_t a, b, c;
    };
    struct CameraData
    {
        glm::mat4 viewProjectionMatrix;
    };

    // App state
    bool bPausing = false;

    // Core components
    AssetManager     assetManager;
    Neon::ImguiState imguiState;
    EditorCamera     camera;
    InputManager     inputManager;
    SDLRender3D   *render;

    // Rendering resources
    SDL_GPUTexture *faceTexture  = nullptr;
    SDL_GPUTexture *whiteTexture = nullptr;

    // Model management
    std::shared_ptr<Model> currentModel;
    bool                   useModel = false;

    // Dialog window for file operations
    std::unique_ptr<NeonEngine::DialogWindow> dialogWindow;

    // Render data
    std::vector<VertexEntry> vertices;
    std::vector<IndexEntry>  indices;
    CameraData               cameraData;
    glm::mat4                quadTransform = glm::mat4(1.0f);

    // Performance tracking
    Uint64 lastTime = 0;
    float  avgFps   = 0.0f;

  public:
    App();
    ~App();

    SDL_AppResult init(int argc, char *argv[]);
    SDL_AppResult iterate();
    SDL_AppResult handleEvent(SDL_Event *evt);
    void          quit(SDL_AppResult result);



  private:
    // ImGui UI methods
    bool imguiManipulateVertices();
    void imguiManipulateSwapchain();
    bool imguiManipulateEditorCamera();
    void imguiModelControls();
    bool uploadModelToGPU(std::shared_ptr<Model> model, std::shared_ptr<CommandBuffer> commandBuffer);
};

} // namespace NeonEngine