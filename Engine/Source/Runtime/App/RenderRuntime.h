#pragma once

#include "Core/Base.h"

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
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
struct EditorLayer;
struct ForwardRenderPipeline;
struct Texture;
struct IRenderTarget;
struct IImageView;
struct IRenderPass;
struct DeferredRenderPipeline;
struct RenderDocCapture;
struct Sampler;

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
    };

    App* _app = nullptr;

    IRender*                                                _render = nullptr;
    std::vector<std::shared_ptr<ICommandBuffer>>            _commandBuffers;
    std::shared_ptr<ShaderStorage>                          _shaderStorage = nullptr;
    std::vector<std::pair<std::string, IGraphicsPipeline*>> _monitorPipelines;

    ERenderAPI::T currentRenderAPI     = ERenderAPI::None;
    EShadingModel _shadingModel        = EShadingModel::Deferred;
    EShadingModel _pendingShadingModel = EShadingModel::Deferred;

    stdptr<ForwardRenderPipeline>  _forwardPipeline  = nullptr;
    stdptr<DeferredRenderPipeline> _deferredPipeline = nullptr;

    stdptr<IDescriptorPool>       _skyboxDSP     = nullptr;
    stdptr<IDescriptorSetLayout> _skyboxDSL     = nullptr;
    stdptr<Sampler>              _skyboxSampler = nullptr;

    Rect2D _viewportRect{};
    float  _viewportFrameBufferScale = 1.0f;

    std::shared_ptr<IRenderPass>   _screenRenderPass = nullptr;
    std::shared_ptr<IRenderTarget> _screenRT         = nullptr;

    stdptr<RenderDocCapture> _renderDocCapture;
    int                      _renderDocOnCaptureAction = 0;
    std::string              _renderDocLastCapturePath;
    std::string              _renderDocConfiguredDllPath;
    std::string              _renderDocConfiguredOutputDir;

    void init(const InitDesc& desc);
    void shutdown();
    void onViewportResized(Rect2D rect);
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
    [[nodiscard]] Texture*                       getPostprocessOutputTexture() const;
    [[nodiscard]] bool                           isPostprocessingEnabled() const;
    [[nodiscard]] stdptr<IDescriptorPool>        getSkyboxDescriptorPool() const { return _skyboxDSP; }
    [[nodiscard]] stdptr<IDescriptorSetLayout>   getSkyboxDescriptorSetLayout() const { return _skyboxDSL; }
    [[nodiscard]] Sampler*                       getSkyboxSampler() const { return _skyboxSampler.get(); }
    [[nodiscard]] const Rect2D&                  getViewportRect() const { return _viewportRect; }
    [[nodiscard]] float                          getViewportFrameBufferScale() const { return _viewportFrameBufferScale; }
    [[nodiscard]] Extent2D                       getViewportExtent() const;

  private:
    void initActivePipeline();
    void shutdownActivePipeline();
    void applyPendingShadingModelSwitch();
};

} // namespace ya
