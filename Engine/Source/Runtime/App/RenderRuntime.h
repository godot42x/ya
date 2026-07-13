#pragma once

#include "Core/Base.h"

#include "Editor/EditorLayer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Render.h"
#include "Render/Shader.h"
#include "Runtime/App/Common/IRenderPipeline.h"
#include "Runtime/App/DeferredRender/DeferredGBufferResources.h"
#include "Runtime/App/DeferredRender/DeferredViewportResources.h"
#include "Runtime/App/OffscreenTaskService.h"
#include "Runtime/App/RenderDiagnosticsService.h"
#include "Runtime/App/RenderSharedResourceProvider.h"

#include <functional>
#include <glm/glm.hpp>
#include <memory>

namespace ya
{

struct App;
struct AppDesc;
enum AppMode : int;
struct SceneManager;
struct Scene;
struct EditorLayer;
struct ForwardRenderPipeline;
struct Texture;
struct RenderImage;
struct IRenderTarget;
struct IImageView;
struct IRenderPass;
struct DeferredRenderPipeline;
struct Sampler;
struct EnvironmentLightingComponent;
struct RenderFrameData;
struct DebugRenderSystem;
struct RenderDiagnosticsService;

struct DeferredPipelineDebugViews
{
    DeferredGBufferResources  gBufferResources{};
    DeferredViewportResources viewportResources{};
    RenderImage*              ssaoTexture = nullptr;
};

struct RenderPipelineDebugOutputCatalog
{
    bool           bShadowMappingEnabled  = false;
    Texture*       shadowDepthTexture     = nullptr;
    Texture*       viewportDepthTexture   = nullptr;
    IImageView*    shadowDirectionalDepth = nullptr;
    Texture*       postprocessOutput      = nullptr;
    RenderImage*   bloomExtract           = nullptr;
    RenderImage*   bloomBlur              = nullptr;
    RenderImage*   bloomComposite         = nullptr;
    bool           bPostprocessingEnabled = false;
};

struct RenderOverlaySprite2D
{
    glm::vec2 viewportPos = glm::vec2(0.0f);
    glm::vec2 size        = glm::vec2(50.0f);
    Texture*  texture     = nullptr;
    glm::vec4 tint        = glm::vec4(1.0f);
};

struct RenderOverlaySprite3D
{
    glm::mat4 worldTransform = glm::mat4(1.0f);
    Texture*  texture        = nullptr;
    glm::vec4 tint           = glm::vec4(1.0f);
};

struct RenderTargetEditorCatalog
{
    struct Entry
    {
        const char*    label = "";
        IRenderTarget* rt    = nullptr;
        enum class EOwner
        {
            Presentation,
            ForwardViewport,
            ForwardShadow,
            DeferredGBuffer,
            DeferredViewport,
            DeferredShadow,
        } owner = EOwner::Presentation;
        bool bEditable = true;
    };

    std::vector<Entry> entries;
};

struct RenderRuntime
{
    enum class ERenderPipeline
    {
        Forward,
        Deferred
    };

    struct InitDesc
    {
        App*           app     = nullptr;
        const AppDesc* appDesc = nullptr;
    };

    struct FrameInput
    {
        struct OverlayInput
        {
            const std::vector<RenderOverlaySprite2D>* screenSprites = nullptr;
            const std::vector<RenderOverlaySprite3D>* worldSprites  = nullptr;
        } overlay{};

        struct EditorInput
        {
            EditorLayer* target = nullptr;
        } editor{};

        struct AutomationInput
        {
            std::function<void(ICommandBuffer*)> recordPresentationCapture;
        } automation{};

        RenderPipelineFrameContext pipeline{};
    };

    App* _app = nullptr;

    ut::StackDeleter _deleter;

    IRender*                                     _render = nullptr;
    OffscreenTaskService                         _offscreen{};
    std::vector<std::shared_ptr<ICommandBuffer>> _commandBuffers;
    std::shared_ptr<ShaderStorage>               _shaderStorage = nullptr;

    ERenderAPI::T  currentRenderAPI      = ERenderAPI::None;
    ERenderPipeline _renderPipeline      = ERenderPipeline::Deferred;
    ERenderPipeline _pendingRenderPipeline = ERenderPipeline::Deferred;

    stdptr<ForwardRenderPipeline>  _forwardPipeline  = nullptr;
    stdptr<DeferredRenderPipeline> _deferredPipeline = nullptr;

    struct RenderTargetEditorState
    {
        int  selectedTargetIndex     = 0;
        int  selectedAttachmentIndex = 0;
        char targetSearch[64]        = {};
        char formatSearch[64]        = {};
    };

    RenderSharedResourceProvider  _sharedResourceProvider{};
    RenderDiagnosticsService     _diagnostics{};

    Rect2D _viewportRect{};
    float  _viewportFrameBufferScale = 1.0f;

    std::shared_ptr<IRenderPass>   _screenRenderPass = nullptr;
    std::shared_ptr<IRenderTarget> _screenRT         = nullptr;

    RenderTargetEditorState _rtEditor{};

    void init(const InitDesc& desc);
    void shutdown();
    void renderFrame(const FrameInput& input);
    void renderGUI(float dt);

  public:
    void onViewportResized(Rect2D rect);
    void resetSkyboxPool();
    void resetEnvironmentLightingPool();

    [[nodiscard]] IRender*                       getRender() const { return _render; }
    [[nodiscard]] std::shared_ptr<ShaderStorage> getShaderStorage() const { return _shaderStorage; }
    [[nodiscard]] IRenderPipeline*               getActivePipeline() const;
    [[nodiscard]] IRenderPipelineExecution*      getActivePipelineExecution() const;
    [[nodiscard]] IRenderPipelineSettingsUI*     getActivePipelineSettingsUI() const;
    [[nodiscard]] IRenderPipelineDebugUI*        getActivePipelineDebugUI() const;
    [[nodiscard]] IRenderPipelineDebugOutputs*   getActivePipelineDebugOutputs() const;
    [[nodiscard]] bool                           isShadowMappingEnabled() const;
    [[nodiscard]] bool                           isMirrorRenderingEnabled() const;
    [[nodiscard]] bool                           hasMirrorRenderResult() const;
    [[nodiscard]] IImageView*                    getShadowDirectionalDepthIV() const;
    [[nodiscard]] IImageView*                    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const;
    [[nodiscard]] bool                           isOffscreenPending() const { return _offscreen.isPending(); }
    [[nodiscard]] OffscreenTaskService&          getOffscreenTaskService() { return _offscreen; }
    [[nodiscard]] const OffscreenTaskService&    getOffscreenTaskService() const { return _offscreen; }
    [[nodiscard]] RenderDiagnosticsService&      getDiagnosticsService() { return _diagnostics; }
    [[nodiscard]] const RenderDiagnosticsService& getDiagnosticsService() const { return _diagnostics; }

    [[nodiscard]] Texture* getPostprocessOutputTexture() const;
    [[nodiscard]] Texture* getActiveViewportTexture() const;
    [[nodiscard]] Texture* getPresentationTexture() const;
    [[nodiscard]] bool     isPostprocessingEnabled() const;
    [[nodiscard]] RenderPipelineDebugOutputCatalog buildPipelineDebugOutputCatalog() const;
    [[nodiscard]] ERenderPipeline getRenderPipeline() const { return _renderPipeline; }
    [[nodiscard]] ERenderPipeline getPendingRenderPipeline() const { return _pendingRenderPipeline; }
    void setPendingRenderPipeline(ERenderPipeline renderPipeline) { _pendingRenderPipeline = renderPipeline; }
    void renderWorldSettingsGUI();
    void renderProfilingDetailsGUI();
    void renderRenderingInternalsGUI();

    [[nodiscard]] stdptr<IDescriptorPool>      getSkyboxDescriptorPool() const { return _sharedResourceProvider.getSkyboxDescriptorPool(); }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkyboxDescriptorSetLayout() const { return _sharedResourceProvider.getSkyboxDescriptorSetLayout(); }
    [[nodiscard]] Sampler*                     getSkyboxSampler() const { return _sharedResourceProvider.getSkyboxSampler(); }
    [[nodiscard]] DescriptorSetHandle          getFallbackSkyboxDescriptorSet() const { return _sharedResourceProvider.getFallbackSkyboxDescriptorSet(); }
    [[nodiscard]] DescriptorSetHandle          getSceneSkyboxDescriptorSet(Scene* scene = nullptr);
    [[nodiscard]] stdptr<IDescriptorSetLayout> getEnvironmentLightingDescriptorSetLayout() const { return _sharedResourceProvider.getEnvironmentLightingDescriptorSetLayout(); }
    [[nodiscard]] DescriptorSetHandle          getSceneEnvironmentLightingDescriptorSet(Scene* scene = nullptr);
    [[nodiscard]] DebugRenderSystem&           getDebugRenderSystem() const;


    [[nodiscard]] const Rect2D& getViewportRect() const { return _viewportRect; }
    [[nodiscard]] float         getViewportFrameBufferScale() const { return _viewportFrameBufferScale; }
    [[nodiscard]] Extent2D      getViewportExtent() const;
    [[nodiscard]] IRenderTarget* getActiveViewportRT() const;
    [[nodiscard]] DeferredPipelineDebugViews getDeferredPipelineDebugViews() const;
    [[nodiscard]] RenderTargetEditorCatalog buildRenderTargetEditorCatalog() const;
    void                          setDeferredSharedDepthFormat(EFormat::T format);
    [[nodiscard]] bool            isDeferredPipelineActive() const { return _renderPipeline == ERenderPipeline::Deferred; }
    [[nodiscard]] ForwardRenderPipeline* getForwardPipelineImpl() const { return _forwardPipeline.get(); }

  private:
    void                   initRuntimeState(const InitDesc& desc);
    void                   initShaderSystems();
    void                   initDiagnostics(const AppDesc& appDesc);
    void                   initRenderBackend(const AppDesc& appDesc);
    void                   initResourceCaches();
    void                   initSharedRenderResources();
    void                   initPresentationResources();
    void                   initCommandResources();
    void                   initFrameServices();
    void                   shutdownRuntimeServices();
    void                   destroyRenderBackend();
    bool                   prepareFrame(const FrameInput& input, int32_t& imageIndex, std::shared_ptr<ICommandBuffer>& cmdBuf);
    void                   renderWorldFrame(const FrameInput& input, ICommandBuffer* cmdBuf);
    void                   syncEditorFrame(EditorLayer* editorLayer);
    void                   ensureViewportRectInitialized(const FrameInput& input);
    bool                   beginFrameCommandBuffer(int32_t& imageIndex, std::shared_ptr<ICommandBuffer>& cmdBuf);
    void                   beginViewportPassAndTickPipeline(const FrameInput& input, ICommandBuffer* cmdBuf);
    void                   renderViewportPassOverlays(const RenderPipelineFrameContext& pipelineFrame, const FrameInput::OverlayInput& overlay, ICommandBuffer* cmdBuf);
    void                   renderPresentationPass(float deltaTime,
                                                  const std::function<void(ICommandBuffer*)>& recordPresentationCapture,
                                                  ICommandBuffer* cmdBuf);
    void                   submitFrame(int32_t imageIndex, ICommandBuffer* cmdBuf);

    void updateEditorViewportContext(EditorLayer* editorLayer);
    void appendForwardDebugSlots(EditorViewportContext& ctx);
    void appendDeferredDebugSlots(EditorViewportContext& ctx);
    void appendEnvironmentDebugSlots(EditorViewportContext& ctx);

    void                   initActivePipeline();
    void                   shutdownActivePipeline();
    void                   applyPendingRenderPipelineSwitch();
    void                   renderRenderTargetEditor();
};

} // namespace ya
