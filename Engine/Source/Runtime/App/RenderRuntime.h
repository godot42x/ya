#pragma once

#include "Core/Base.h"

#include "Render/Core/OffscreenJob.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Pipelines/PBRGenerateBrdfLUT.h"
#include "Render/Render.h"
#include "Render/Shader.h"

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
struct IRenderTarget;
struct IImageView;
struct IRenderPass;
struct DeferredRenderPipeline;
struct RenderDocCapture;
struct Sampler;
struct EnvironmentLightingComponent;
struct RenderFrameData;

struct RenderRuntime
{
    enum class EShadingModel
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
        float                   dt                       = 0.0f;
        SceneManager*           sceneManager             = nullptr;
        EditorLayer*            editorLayer              = nullptr;
        Rect2D                  viewportRect             = {};
        float                   viewportFrameBufferScale = 1.0f;
        AppMode                 appMode                  = {};
        std::vector<glm::vec2>* clicked                  = nullptr;
        glm::mat4               view                     = glm::mat4(1.0f);
        glm::mat4               projection               = glm::mat4(1.0f);
        glm::vec3               cameraPos                = glm::vec3(0.0f);
        RenderFrameData*        frameData                = nullptr;
    };

    App* _app = nullptr;

    ut::StackDeleter _deleter;

    IRender*                                     _render = nullptr;
    stdptr<ICommandBuffer>                       _offscreenCmdBuf;
    void*                                        _offscreenFence   = nullptr;
    bool                                         _offscreenPending = false;
    std::vector<std::shared_ptr<OffscreenJobState>> _submittedOffscreenJobs;
    std::vector<std::shared_ptr<ICommandBuffer>> _commandBuffers;
    std::shared_ptr<ShaderStorage>               _shaderStorage = nullptr;

    ERenderAPI::T currentRenderAPI     = ERenderAPI::None;
    EShadingModel _shadingModel        = EShadingModel::Deferred;
    EShadingModel _pendingShadingModel = EShadingModel::Deferred;

    stdptr<ForwardRenderPipeline>  _forwardPipeline  = nullptr;
    stdptr<DeferredRenderPipeline> _deferredPipeline = nullptr;

    struct SkyboxResources
    {
        stdptr<IDescriptorPool>      dsp               = nullptr;
        stdptr<IDescriptorSetLayout> dsl               = nullptr;
        stdptr<Texture>              fallbackTexture   = nullptr;
        DescriptorSetHandle          fallbackDS        = nullptr;
        DescriptorSetHandle          sceneDS           = nullptr;
        Texture*                     boundSceneTexture = nullptr;
    };

    struct EnvironmentLightingResources
    {
        stdptr<IDescriptorPool>      dsp                       = nullptr;
        stdptr<IDescriptorSetLayout> dsl                       = nullptr;
        DescriptorSetHandle          fallbackDS                = nullptr;
        DescriptorSetHandle          sceneDS                   = nullptr;
        stdptr<Texture>              fallbackIrradianceTexture = nullptr;
        stdptr<Texture>              fallbackPrefilterTexture  = nullptr;
        Texture*                     boundCubemapTexture       = nullptr;
        Texture*                     boundIrradianceTexture    = nullptr;
        Texture*                     boundPrefilterTexture     = nullptr;
    };
    struct SharedResources
    {
        stdptr<Texture> pbrLUT = nullptr;
    };

    SkyboxResources              _skybox{};
    EnvironmentLightingResources _environmentLighting{};
    SharedResources              _sharedResources{};
    stdptr<Sampler>              _cubemapSampler = nullptr;

    // pipeline tool
    PBRGenerateBrdfLUT           _pbrGenerateBrdfLUT{};


    Rect2D _viewportRect{};
    float  _viewportFrameBufferScale = 1.0f;

    std::shared_ptr<IRenderPass>   _screenRenderPass = nullptr;
    std::shared_ptr<IRenderTarget> _screenRT         = nullptr;

    int  _rtEditorSelectedTargetIndex     = 0;
    int  _rtEditorSelectedAttachmentIndex = 0;
    char _rtEditorTargetSearch[64]        = {};
    char _rtEditorFormatSearch[64]        = {};

    stdptr<RenderDocCapture> _renderDocCapture;
    int                      _renderDocOnCaptureAction = 0;
    std::string              _renderDocLastCapturePath;
    std::string              _renderDocConfiguredDllPath;
    std::string              _renderDocConfiguredOutputDir;

    void init(const InitDesc& desc);
    void shutdown();
    void onViewportResized(Rect2D rect);
    void offScreenRender();
    void finalizeCompletedOffscreenJobs();
    void renderFrame(const FrameInput& input);
    void renderGUI(float dt);

    [[nodiscard]] IRender*                       getRender() const { return _render; }
    [[nodiscard]] std::shared_ptr<ShaderStorage> getShaderStorage() const { return _shaderStorage; }
    [[nodiscard]] ForwardRenderPipeline*         getForwardPipeline() const;
    [[nodiscard]] bool                           isShadowMappingEnabled() const;
    [[nodiscard]] bool                           isMirrorRenderingEnabled() const;
    [[nodiscard]] bool                           hasMirrorRenderResult() const;
    [[nodiscard]] IRenderTarget*                 getShadowDepthRT() const;
    [[nodiscard]] IImageView*                    getShadowDirectionalDepthIV() const;
    [[nodiscard]] IImageView*                    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const;
    [[nodiscard]] bool                           isOffscreenPending() const { return _offscreenPending; }

    [[nodiscard]] Texture* getPostprocessOutputTexture() const;
    [[nodiscard]] bool     isPostprocessingEnabled() const;

    [[nodiscard]] stdptr<IDescriptorPool>      getSkyboxDescriptorPool() const { return _skybox.dsp; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkyboxDescriptorSetLayout() const { return _skybox.dsl; }
    [[nodiscard]] Sampler*                     getSkyboxSampler() const { return _cubemapSampler.get(); }
    [[nodiscard]] DescriptorSetHandle          getFallbackSkyboxDescriptorSet() const { return _skybox.fallbackDS; }
    [[nodiscard]] DescriptorSetHandle          getSceneSkyboxDescriptorSet(Scene* scene = nullptr);
    [[nodiscard]] stdptr<IDescriptorSetLayout> getEnvironmentLightingDescriptorSetLayout() const { return _environmentLighting.dsl; }
    [[nodiscard]] DescriptorSetHandle          getSceneEnvironmentLightingDescriptorSet(Scene* scene = nullptr);

    /**
     * @brief Reset the skybox descriptor pool and re-allocate the fallback DS.
     *
     * Must be called when a scene is about to be destroyed so that any
     * SkyboxComponent descriptor sets allocated from this pool are returned.
     * Without this, each scene load leaks one DS until the pool overflows.
     */
    void                        resetSkyboxPool();
    void                        resetEnvironmentLightingPool();
    [[nodiscard]] const Rect2D& getViewportRect() const { return _viewportRect; }
    [[nodiscard]] float         getViewportFrameBufferScale() const { return _viewportFrameBufferScale; }
    [[nodiscard]] Extent2D      getViewportExtent() const;

  private:
    void                   initActivePipeline();
    void                   shutdownActivePipeline();
    void                   releaseRenderOwnedResources();
    void                   applyPendingShadingModelSwitch();
    void                   renderRenderTargetEditor();
    void                   updateSkyboxDescriptorSet(DescriptorSetHandle ds, Texture* texture);
    void                   updateEnvironmentLightingDescriptorSet(DescriptorSetHandle ds, Texture* cubemapTexture, Texture* irradianceTexture, Texture* prefilterTexture, Texture* brdfLutTexture);
    [[nodiscard]] Texture* findSceneSkyboxTexture(Scene* scene) const;
    [[nodiscard]] Texture* findSceneEnvironmentCubemapTexture(Scene* scene) const;
    [[nodiscard]] Texture* findSceneEnvironmentIrradianceTexture(Scene* scene) const;
    [[nodiscard]] Texture* findSceneEnvironmentPrefilterTexture(Scene* scene) const;
};

} // namespace ya
