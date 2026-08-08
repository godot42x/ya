#pragma once

#include "Foundation/RHI/Core/DescriptorSet.h"
#include "Foundation/RHI/Core/RenderAttachmentFormats.h"
#include "Foundation/RHI/Core/Pipeline.h"
#include "Framework/Game/Render/Render3D/Material/SimpleMaterial.h"
#include "Framework/Game/Render/Render3D/Common/IRenderRuntimeServices.h"
#include "GLSL.Skybox.glsl.h"

#include <array>
#include <vector>
#include <glm/glm.hpp>

namespace ya
{

struct RenderStageContext;
struct RenderDrawItem;
class ForwardFrameResourceSet;
class Scene;
class Mesh;
class IBuffer;
class IDescriptorSetLayout;
class IDescriptorPool;
class IRenderPass;
class IRender;

/// Direction gizmo snapshot built before graph execution (FG-704). Mirrors
/// the Deferred overlay snapshot: the pass draws only from this prebuilt
/// input, never by querying the ECS during execute.
struct ForwardDirectionGizmoInput
{
    glm::mat4 coneModel     = glm::mat4(1.0f);
    glm::mat4 cylinderModel = glm::mat4(1.0f);
    glm::vec3 lineStart     = glm::vec3(0.0f);
    glm::vec3 lineEnd       = glm::vec3(0.0f);
};

class YA_RENDER_3D_API ForwardViewportAuxPasses
{
  public:
    struct SimplePC
    {
        glm::mat4 projection = glm::mat4(1.0f);
        glm::mat4 view       = glm::mat4(1.0f);
        glm::mat4 model      = glm::mat4(1.0f);
        uint32_t  colorType  = 0;
    };

    using SkyboxFrameUBO = glsl_types::GLSL::Skybox::FrameUBO;

    struct DebugUBO
    {
        glm::mat4 projection{1.f};
        glm::mat4 view{1.f};
        alignas(8) glm::ivec2 resolution{0, 0};
        alignas(4) int mode   = 0;
        alignas(4) float time = 0.f;
        glm::vec4 floatParam  = glm::vec4(0.0f);
    };

    enum EDebugMode
    {
        DebugNone = 0,
        DebugNormalColor,
        DebugNormalDir,
        DebugDepth,
        DebugUV,
    };

    struct DebugModelPC
    {
        glm::mat4 modelMat;
    };

    struct InitDesc
    {
        IRender*               render = nullptr;
        IRenderPass*           renderPass = nullptr;
        PipelineRenderingInfo  pipelineRenderingInfo = {};
        stdptr<IDescriptorSetLayout> skyboxFrameDSL;
        IRenderRuntimeServices* runtimeServices = nullptr;
    };

    struct DrawContext
    {
        struct SkyboxInput
        {
            bool                bAvailable = false;
            DescriptorSetHandle descriptorSet = nullptr;
            Mesh*               mesh = nullptr;
        };

        struct DebugDrawInput
        {
            struct Bucket
            {
                const std::vector<RenderDrawItem>* items = nullptr;
                bool                               bSkinned = false;
            };

            std::array<Bucket, 10> buckets{};
            uint32_t               count = 0;
            bool                   bHasDraws = false;
        };

        const RenderStageContext& stageCtx;
        Scene*                    activeScene = nullptr;
        SkyboxInput               skybox{};
        DebugDrawInput            debugDraw{};
        std::vector<ForwardDirectionGizmoInput> directionGizmos{};
        DescriptorSetHandle       skyboxFrameDescriptorSet = nullptr;
        bool                      bReverseViewportY = true;
    };

    void init(const InitDesc& desc);
    void destroy();
    void beginFrame();
    void refreshPipelineFormats(const RenderAttachmentFormats& formats);
    /// Build the current frame's skybox payload; the pipeline uploads it
    /// through ForwardFrameResourceSet (FG-701).
    void prepare(const RenderStageContext& ctx, SkyboxFrameUBO& outFrame);

    void drawSkybox(const DrawContext& ctx);
    void drawSimple(const DrawContext& ctx);
    void drawDirectionOverlay(const DrawContext& ctx);
    void drawDebug(const DrawContext& ctx);

    [[nodiscard]] int getSimpleDefaultColorType() const { return _simpleDefaultColorType; }
    void              setSimpleDefaultColorType(int colorType) { _simpleDefaultColorType = colorType; }

    [[nodiscard]] EDebugMode getDebugMode() const { return _debugMode; }
    void                     setDebugMode(EDebugMode mode);
    [[nodiscard]] DebugUBO&       getDebugUBO() { return _debugUBO; }
    [[nodiscard]] const DebugUBO& getDebugUBO() const { return _debugUBO; }

    [[nodiscard]] IGraphicsPipeline* getSimplePipeline() const { return _simplePipeline.get(); }
    [[nodiscard]] IGraphicsPipeline* getSkyboxPipeline() const { return _skyboxPipeline.get(); }
    [[nodiscard]] IGraphicsPipeline* getDebugPipeline() const { return _debugPipeline.get(); }

  private:
    void initSimple(const InitDesc& desc);
    void initSkybox(const InitDesc& desc);
    void initDebug(const InitDesc& desc);

    IRender* _render = nullptr;
    IRenderRuntimeServices* _runtimeServices = nullptr;

    stdptr<IPipelineLayout>   _simplePPL;
    stdptr<IGraphicsPipeline> _simplePipeline;
    int                       _simpleDefaultColorType = 0;

    stdptr<IDescriptorSetLayout> _skyboxFrameDSL;
    stdptr<IDescriptorSetLayout> _skyboxResourceDSL;
    stdptr<IPipelineLayout>      _skyboxPPL;
    stdptr<IGraphicsPipeline>    _skyboxPipeline;

    stdptr<IDescriptorSetLayout> _debugDSL;
    stdptr<IPipelineLayout>      _debugPPL;
    stdptr<IGraphicsPipeline>    _debugPipeline;
    GraphicsPipelineCreateInfo   _debugPipelineCI;
    stdptr<IDescriptorPool>      _debugDSP;
    DescriptorSetHandle          _debugUboDS = nullptr;
    stdptr<IBuffer>              _debugUboBuffer;
    DebugUBO                     _debugUBO{};
    EDebugMode                   _debugMode = DebugNone;
};

} // namespace ya
