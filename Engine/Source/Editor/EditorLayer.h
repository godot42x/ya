#pragma once

#include "AssetInspectorPanel.h"
#include "ContentBrowserPanel.h"
#include "Core/Base.h"

#include "Core/Event.h"
#include "DetailsView.h"
#include "FilePicker.h"
#include "ImGuiHelper.h"
#include "Render/Core/Image.h"
#include "Render/Core/TextureFactory.h"
#include "SceneHierarchyPanel.h"
#include <imgui.h>

#include "Editor/EditorCommon.h"

#include <ImGuizmo.h>



namespace ya
{

struct App;
struct Texture;
struct IRenderTarget;
struct IImageView;

// All render resources that the editor viewport needs, explicitly passed in from App
struct EditorViewportContext
{
    // Which pipeline is active?
    bool bForwardPipeline = false;

    // Viewport output
    Texture* viewportTexture = nullptr;

    // Postprocessing
    bool     bPostprocessingEnabled   = false;
    Texture* postprocessOutputTexture = nullptr;

    // Shadow mapping
    bool           bShadowMappingEnabled    = false;
    IRenderTarget* shadowDepthRT            = nullptr;
    IImageView*    shadowDirectionalDepthIV = nullptr;
    // Point light shadow: indexed [pointLightIndex][faceIndex]
    // Caller provides a lambda to avoid large fixed-size arrays
    std::function<IImageView*(uint32_t /*pointLightIndex*/, uint32_t /*faceIndex*/)> getShadowPointFaceDepthIV;

    // Mirror
    bool           bMirrorRenderResult = false;
    IRenderTarget* mirrorRenderTarget  = nullptr;


    struct GBufferSlot
    {
        std::string             label;
        IImageView*             defaultView = nullptr; // identity view from Texture
        std::shared_ptr<IImage> image;                 // for createImageView()
    };

    struct DeferredSpec
    {
        std::vector<GBufferSlot> slots;
    } deferredSpec;
};

struct EditorLayer
{
  private:
    App*                 _app = nullptr;
    std::vector<Entity*> _selections;

    // Editor panels
    SceneHierarchyPanel  _sceneHierarchyPanel;
    DetailsView          _detailsView;
    ContentBrowserPanel  _contentBrowserPanel;
    AssetInspectorPanel  _assetInspectorPanel;

    // ImGui Docking state
    ImGuiDockNodeFlags _dockspaceFlags = ImGuiDockNodeFlags_None;
    ImGuiWindowFlags   _windowFlags    = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    // Window state
    bool bFullscreen     = true;
    bool bPadding        = true;
    bool bShowDemoWindow = false;
    // bool bShowSettingsWindow = true;
    // bool bShowRenderStats    = true;

    // Viewport state
    glm::vec2 _viewportSize = {1280.f, 720.f};
    glm::vec2 _viewportBounds[2]; // Min and max bounds
    Rect2D    viewportRect;
    bool      bViewportFocused     = false;
    bool      bViewportHovered     = false;
    bool      _bRightMouseDragging = false; // Track right mouse drag for camera rotation
    glm::vec2 _rightMousePressPos  = {};    // Position when right mouse was pressed

    // Editor settings
    glm::vec4 _clearColor = {0.1f, 0.1f, 0.1f, 1.0f};
    float     _debugFloat = 0.0f;

    // ImGui texture descriptor set cache (editor-only, application layer)
    std::unordered_set<ImGuiImageEntry> _imguiTextureCache; // ImageView → VkDescriptorSet

    std::function<void()> _contentFunc;

    // Gizmo state
    ImGuizmo::OPERATION _gizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE      _gizmoMode      = ImGuizmo::LOCAL;

    const ImGuiImageEntry* _playIcon       = nullptr;
    const ImGuiImageEntry* _pauseIcon      = nullptr;
    const ImGuiImageEntry* _stopIcon       = nullptr;
    const ImGuiImageEntry* _simulationIcon = nullptr;
    enum
    {
        Linear = 0,
        Nearest
    } _viewPortSamplerType = Linear;

    uint32_t _resizeTimerHandle = 0;
    Rect2D   _pendingViewportRect; // Pending resize event to be processed in next frame
    bool     _bViewportResizePending = false;

    // Render resources explicitly passed in from App each frame
    EditorViewportContext _viewportCtx;

    // Per-slot state for the deferred GBuffer debug viewer (RGBA toggle mask + cached swizzled view)
    struct GbufferSlotState
    {
        std::string                  configKey;
        std::array<bool, 4>          channelEnabled = {true, true, true, true};
        std::shared_ptr<IImageView> maskedView;
        IImageView*                  lastBase = nullptr;
    };
    std::vector<GbufferSlotState> _deferredSlotStates;

  public:
    Delegate<void(Rect2D /*rect*/)> onViewportResized;

    // File picker for save/load dialogs and asset selection
    FilePicker  _filePicker;
    std::string _currentScenePath; // Current scene file path

  public:
    EditorLayer(App* app);
    ~EditorLayer() = default;

    void onAttach();
    void onDetach();

    /// Open the Asset Inspector for the given relative path
    void inspectAsset(const std::string& relativePath) { _assetInspectorPanel.inspectTexture(relativePath); }

    // Set viewport render context before ImGui render - called from App each frame
    void setViewportContext(const EditorViewportContext& ctx) { _viewportCtx = ctx; }

    void onUpdate(float dt);
    void setSceneContext(Scene* scene)
    {
        _sceneHierarchyPanel.setContext(scene);
    }

    // Entity selection bus - notifies DetailsView of selection changes
    void setSelectedEntity(Entity* entity)
    {
        // 验证实体有效性，防止悬空指针
        if (entity && entity->isValid()) {
            _selections = {entity};
        }
        else {
            _selections = {nullptr};
        }
    }

    /**
     * @brief Check if viewport should capture input events
     * @return true if viewport is focused and should receive events
     */
    bool shouldCaptureInput() const { return bViewportFocused; }

    // Get and clear pending viewport resize - called from App before render
    bool getPendingViewportResize(Rect2D& outRect)
    {
        if (_bViewportResizePending) {
            outRect                 = _pendingViewportRect;
            _bViewportResizePending = false;
            return true;
        }
        return false;
    }

    bool screenToViewport(float screenX, float screenY, float& outX, float& outY) const;
    bool screenToViewport(const glm::vec2 in, glm::vec2& out) const;

    void onImGuiRender(auto content)
    {
        YA_PROFILE_FUNCTION();
        // ya::DeferredModificationQueue::get().onFrameBegin();

        {
            ya::ImGuiStyleScope style;
            updateWindowFlags(style);

            // Begin main dockspace window
            ImGui::Begin("MainEditor", nullptr, _windowFlags);
        }


        setupDockspace();
        menuBar();
        toolbar();
        editorSettings();
        viewportWindow();
        debugWindow();

        _sceneHierarchyPanel.onImGuiRender();
        _detailsView.onImGuiRender();
        _contentBrowserPanel.onImGuiRender();
        _assetInspectorPanel.onImGuiRender();

        // Render file picker dialog
        _filePicker.render();

        // Render windows
        // settingsWindow();
        // renderStatsWindow();

        content();

        // Demo window option
        if (bShowDemoWindow)
        {
            ImGui::ShowDemoWindow(&bShowDemoWindow);
        }

        ImGui::End(); // End main dockspace window
    }
    void onEvent(const Event& event);

    void setContent(std::function<void()> contentFunc)
    {
        _contentFunc = contentFunc;
    }


    /**
     * @brief Get or create ImGui texture ID for rendering in ImGui::Image()
     * @param imageView Platform image view handle (e.g., VkImageView)
     * @param sampler Platform sampler handle (e.g., VkSampler)
     * @return ImTextureID (VkDescriptorSet as void*)
     */
    const ImGuiImageEntry* getOrCreateImGuiTextureID(ya::Ptr<IImageView> imageView, ya::Ptr<Sampler> sampler = nullptr);
    const ImGuiImageEntry* getOrCreateImGuiDescriptorSet(ya::Ptr<IImageView> imageView, ya::Ptr<Sampler> sampler = nullptr)
    {
        return getOrCreateImGuiTextureID(imageView, sampler);
    }


  private:
    // UI Methods
    void updateWindowFlags(ya::ImGuiStyleScope& style);
    void menuBar();
    void toolbar();
    // void settingsWindow();
    // void renderStatsWindow();
    void viewportWindow();
    void editorSettings();

    // --
    void debugWindow();
    void debugForwardWindow(const ImVec2& panelSize);
    void debugDeferredWindow(const ImVec2& panelSize);
    void syncDeferredSlotState(const EditorViewportContext::GBufferSlot& slot, GbufferSlotState& state);
    bool renderDeferredSlotMaskControls(const EditorViewportContext::GBufferSlot& slot, GbufferSlotState& state);
    void updateDeferredSlotImageView(const EditorViewportContext::GBufferSlot& slot, GbufferSlotState& state, bool bForceRefresh = false);
    void renderDeferredSlotImage(const EditorViewportContext::GBufferSlot& slot, GbufferSlotState& state, float width, float height, Sampler* sampler);

    // Helpers
    void setupDockspace();


    void cleanupImGuiTextures();
    void removeImGuiTexture(const ImGuiImageEntry* entry);
    void renderGizmo();
    void pickEntity(float viewportX, float viewportY);
    void focusCameraOnEntity(Entity* entity);

  public:
    // Public getters
    glm::vec2                   getViewportSize() const { return _viewportSize; }
    bool                        isViewportFocused() const { return bViewportFocused; }
    bool                        isViewportHovered() const { return bViewportHovered; }
    bool                        isGizmoActive() const; // Check if ImGuizmo is being used or hovered
    bool                        isRightMouseDragging() const { return _bRightMouseDragging; }
    const std::vector<Entity*>& getSelections() const { return _selections; }
    // void      setViewportImage(stdptr<IImageView> image) { _viewportImage = getOrCreateImGuiTextureID(image); }
};

} // namespace ya
