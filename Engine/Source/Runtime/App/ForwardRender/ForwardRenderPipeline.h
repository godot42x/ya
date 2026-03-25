#pragma once

#include "Core/Base.h"
#include "ECS/System/Render/IRenderSystem.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Render.h"

#include <array>
#include <glm/glm.hpp>
#include <vector>

namespace ya
{

struct SceneManager;
struct Texture;
struct Sampler;
struct PhongScenePassResources;

struct ForwardRenderPipeline
{
    static const auto COLOR_FORMAT                       = EFormat::R8G8B8A8_UNORM;
    static const auto DEPTH_FORMAT                       = EFormat::D32_SFLOAT_S8_UINT;
    static const auto SHADOW_MAPPING_DEPTH_BUFFER_FORMAT = EFormat::D32_SFLOAT;

    struct InitDesc
    {
        IRender*          render         = nullptr;
        int               windowW        = 0;
        int               windowH        = 0;
    };

    struct TickDesc
    {
        ICommandBuffer*                cmdBuf                    = nullptr;
        float                          dt                        = 0.0f;
        glm::mat4                      view                      = glm::mat4(1.0f);
        glm::mat4                      projection                = glm::mat4(1.0f);
        glm::vec3                      cameraPos                 = glm::vec3(0.0f);
        Rect2D                         viewportRect              = {};
        float                          viewportFrameBufferScale  = 1.0f;
        int                            appMode                   = 0;
        std::vector<glm::vec2>*       clicked                   = nullptr;
    };

    Deleter _deleter;

    IRender*         _render         = nullptr;

    stdptr<IDescriptorPool> _descriptorPool = nullptr;

    std::shared_ptr<IRenderPass>   viewportRenderPass = nullptr;
    std::shared_ptr<IRenderTarget> viewportRT         = nullptr;
    stdptr<Texture>                postprocessTexture = nullptr;

    std::shared_ptr<IRenderPass>   screenRenderPass = nullptr;
    std::shared_ptr<IRenderTarget> screenRT         = nullptr;

    stdptr<IDescriptorSetLayout> skyBoxCubeMapDSL    = nullptr;
    DescriptorSetHandle          skyBoxCubeMapDS     = nullptr;
    stdptr<IDescriptorSetLayout> depthBufferDSL      = nullptr;
    DescriptorSetHandle          depthBufferShadowDS = nullptr;
    stdptr<Sampler>              shadowSampler       = nullptr;

    stdptr<IRenderSystem> simpleMaterialSystem      = nullptr;
    stdptr<IRenderSystem> unlitMaterialSystem       = nullptr;
    stdptr<IRenderSystem> phongMaterialSystem       = nullptr;
    stdptr<IRenderSystem> debugRenderSystem         = nullptr;
    stdptr<IRenderSystem> skyboxSystem              = nullptr;
    stdptr<IRenderSystem> shadowMappingSystem       = nullptr;
    stdptr<IRenderSystem> basicPostprocessingSystem = nullptr;

    stdptr<IBuffer> _phongSharedLightUBO = nullptr;
    stdptr<PhongScenePassResources> _phongViewportPassResources = nullptr;

    bool bMSAA = false;

    bool                  bShadowMapping            = true;
    stdptr<IRenderTarget> depthRT                   = nullptr;
    stdptr<IRenderTarget> pointLightDepthRT         = nullptr;
    stdptr<IImageView>    shadowDirectionalDepthIV  = nullptr;

    std::array<stdptr<IImageView>, MAX_POINT_LIGHTS>                shadowPointCubeIVs{};
    std::array<std::array<stdptr<IImageView>, 6>, MAX_POINT_LIGHTS> shadowPointFaceIVs{};

    Texture* viewportTexture = nullptr;

    // Stored viewport RenderingInfo for App-level endViewportPass()
    RenderingInfo _viewportRI{};
    FrameContext  _lastTickCtx{};
    TickDesc      _lastTickDesc{};

    void init(const InitDesc& desc);
    void tick(const TickDesc& desc);
    void shutdown();
    void renderGUI();

    /// End the viewport rendering pass (called by App after 2D rendering)
    void endViewportPass(ICommandBuffer* cmdBuf);

    bool     recreateViewportRT(uint32_t width, uint32_t height);
    void     onViewportResized(Rect2D rect);
    Extent2D getViewportExtent() const;

  private:
    void renderScene(ICommandBuffer* cmdBuf, float dt, FrameContext& ctx, const stdptr<PhongScenePassResources>& phongScenePassResources);
    void beginFrame();
};

} // namespace ya
