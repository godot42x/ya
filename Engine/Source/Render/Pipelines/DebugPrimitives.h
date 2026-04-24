#pragma once

#include "Core/Math/Geometry.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
#include "Render/Stage/IRenderStage.h"

#include <array>
#include <glm/glm.hpp>
#include <imgui.h>
#include <vector>

namespace ya
{

struct Mesh;

struct DebugPrimitives
{
    struct LineVertex
    {
        glm::vec3 position{0.0f};
        glm::vec4 color{1.0f};
    };

    struct ShapeInstance
    {
        glm::mat4 model{1.0f};
        glm::vec4 color{1.0f};
        EPrimitiveGeometry primitive = EPrimitiveGeometry::None;
    };

    struct ShapePushConstant
    {
        glm::mat4 model{1.0f};
        glm::vec4 color{1.0f};
    };

    struct FrameUBO
    {
        glm::mat4 projection{1.0f};
        glm::mat4 view{1.0f};
    };

    static constexpr EFormat::T LINEAR_FORMAT   = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T DEPTH_FORMAT    = EFormat::D32_SFLOAT;
    static constexpr uint32_t   MAX_LINE_VERTICES = 8192;

    IRender*                     _render = nullptr;
    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorPool>      _frameDSP;
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> _frameDS{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _frameUBO{};
    stdptr<IGraphicsPipeline>    _linePipeline;
    stdptr<IPipelineLayout>   _linePipelineLayout;
    stdptr<IGraphicsPipeline> _shapePipeline;
    stdptr<IPipelineLayout>   _shapePipelineLayout;
    stdptr<IBuffer>           _lineVertexBuffer;
    uint32_t                  _lineVertexCapacity = 0;

    bool bEnabled          = true;
    bool bReverseViewportY = true;
    bool bDepthTest        = true;
    bool bDrawLines        = true;
    bool bDrawShapes       = true;

    // Game-thread collected debug primitives, consumed by render thread next frame.
    std::vector<LineVertex>    _queuedLineVertices;
    std::vector<ShapeInstance> _queuedShapeInstances;
    std::vector<LineVertex>    _frameLineVertices;
    std::vector<ShapeInstance> _frameShapeInstances;

    // Render-thread only: visible in the same frame.
    std::vector<LineVertex>    _immediateLineVertices;
    std::vector<ShapeInstance> _immediateShapeInstances;
    FrameUBO                   _frameData{};

    void init(IRender* render);
    void destroy();
    void beginFrame();
    void clear();
    void refreshPipelineFormats(const IRenderTarget* viewportRT);

    // Game-thread submission path: consumed by render thread on next beginFrame().
    void addLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color = glm::vec4(1.0f));
    void addSphere(const glm::vec3& center, float radius, const glm::vec4& color = glm::vec4(1.0f));
    void addCylinder(const glm::mat4& model, const glm::vec4& color = glm::vec4(1.0f));
    void addCone(const glm::mat4& model, const glm::vec4& color = glm::vec4(1.0f));

    // Render-thread submission path: shows up in the same frame.
    void addLineImmediate(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color = glm::vec4(1.0f));
    void addSphereImmediate(const glm::vec3& center, float radius, const glm::vec4& color = glm::vec4(1.0f));
    void addCylinderImmediate(const glm::mat4& model, const glm::vec4& color = glm::vec4(1.0f));
    void addConeImmediate(const glm::mat4& model, const glm::vec4& color = glm::vec4(1.0f));

    void draw(ICommandBuffer*  cmdBuf,
              uint32_t         viewportWidth,
              uint32_t         viewportHeight,
              const glm::mat4& projection,
              const glm::mat4& view);

    void renderGUI();

  private:
    void initFrameResources();
    void initLinePipeline();
    void initShapePipeline();
    void updateFrameUBO();
    void ensureLineBufferCapacity(uint32_t requiredVertexCount);
    void applyPipelineFormats(stdptr<IGraphicsPipeline>& pipeline, const IRenderTarget* viewportRT);
    void updateDepthState();
    void clearImmediate();
    void setViewportAndScissor(ICommandBuffer* cmdBuf, uint32_t viewportWidth, uint32_t viewportHeight) const;
    void drawLines(ICommandBuffer* cmdBuf,
                   uint32_t viewportWidth,
                   uint32_t viewportHeight,
                   uint32_t flightIndex,
                   const std::vector<LineVertex>& vertices);
    void drawShapes(ICommandBuffer* cmdBuf,
                    uint32_t viewportWidth,
                    uint32_t viewportHeight,
                    uint32_t flightIndex,
                    const std::vector<ShapeInstance>& shapes);
    static glm::mat4 buildSphereModel(const glm::vec3& center, float radius);
};

} // namespace ya
