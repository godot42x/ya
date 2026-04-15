#pragma once

#include "Core/Base.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Pipelines/PostProcessStage.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"
#include "ShadowStage.h"
#include "ForwardViewportStage.h"

#include <array>
#include <glm/glm.hpp>
#include <vector>

namespace ya
{

struct SceneManager;
struct Texture;
struct Sampler;

struct ForwardRenderPipeline
{
    static constexpr auto LINEAR_FORMAT                       = EFormat::R8G8B8A8_UNORM;
    static constexpr auto DEPTH_FORMAT                        = EFormat::D32_SFLOAT_S8_UINT;
    static constexpr auto SHADOW_MAPPING_DEPTH_BUFFER_FORMAT  = EFormat::D32_SFLOAT;

    struct InitDesc
    {
        IRender* render  = nullptr;
        int      windowW = 0;
        int      windowH = 0;
    };

    struct TickDesc
    {
        ICommandBuffer*         cmdBuf                   = nullptr;
        float                   dt                       = 0.0f;
        glm::mat4               view                     = glm::mat4(1.0f);
        glm::mat4               projection               = glm::mat4(1.0f);
        glm::vec3               cameraPos                = glm::vec3(0.0f);
        Rect2D                  viewportRect             = {};
        float                   viewportFrameBufferScale = 1.0f;
        int                     appMode                  = 0;
        std::vector<glm::vec2>* clicked                  = nullptr;
        RenderFrameData*        frameData                = nullptr;
    };

    Deleter _deleter;

    IRender* _render = nullptr;

    stdptr<IDescriptorPool> _descriptorPool = nullptr;

    std::shared_ptr<IRenderTarget> viewportRT = nullptr;

    // Shadow resources (owned here, shared to stages)
    stdptr<IDescriptorSetLayout> depthBufferDSL      = nullptr;
    DescriptorSetHandle          depthBufferShadowDS = nullptr;
    stdptr<Sampler>              shadowSampler       = nullptr;
    bool                         bShadowMapping      = true;
    stdptr<IRenderTarget>        depthRT             = nullptr;
    stdptr<IImageView>           shadowDirectionalDepthIV = nullptr;
    std::array<stdptr<IImageView>, MAX_POINT_LIGHTS>                shadowPointCubeIVs{};
    std::array<std::array<stdptr<IImageView>, 6>, MAX_POINT_LIGHTS> shadowPointFaceIVs{};

    // ── Render stages ─────────────────────────────────────────────
    stdptr<ShadowStage>           _shadowStage;
    stdptr<ForwardViewportStage>  _viewportStage;
    PostProcessStage      _postProcessStage;

    bool     bMSAA = false;
    Texture* viewportTexture    = nullptr;
    bool     _bViewportPassOpen = false;

    RenderingInfo _viewportRI{};
    FrameContext  _lastTickCtx{};
    TickDesc      _lastTickDesc{};

    void init(const InitDesc& desc);
    void tick(const TickDesc& desc);
    void shutdown();
    void renderGUI();

    void endViewportPass(ICommandBuffer* cmdBuf);
    bool hasOpenViewportPass() const { return _bViewportPassOpen; }

    void     onViewportResized(Rect2D rect);
    Extent2D getViewportExtent() const;

    // Shadow query accessors (used by RenderRuntime for debug views)
    [[nodiscard]] bool           isShadowMappingEnabled() const { return bShadowMapping; }
    [[nodiscard]] IRenderTarget* getShadowDepthRT() const { return depthRT.get(); }
    [[nodiscard]] IImageView*    getShadowDirectionalDepthIV() const { return shadowDirectionalDepthIV.get(); }
    [[nodiscard]] IImageView*    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const;
};

} // namespace ya
