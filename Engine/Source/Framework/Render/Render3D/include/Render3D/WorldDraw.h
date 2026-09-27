#pragma once

#include "Core/Base.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/RenderDefines.h"

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace ya
{

struct IRender;
struct ICommandBuffer;

/// One world-space line vertex. Sprite2DLine.slang consumes this layout.
struct WorldDrawVertex
{
    glm::vec3 pos;
    glm::vec4 color;
};

/// CPU list of world-space debug lines. Drawing it requires a view-projection.
/// Immediate: built and discarded every frame, not a scene snapshot.
struct YA_RENDER_3D_API WorldDrawList
{
    struct Command
    {
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
    };

    std::vector<Command>        commands;
    std::vector<WorldDrawVertex> vertices;

    void makeLine(const glm::vec3& from,
                  const glm::vec3& to,
                  const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f});
    void makeWireBox(const glm::mat4& model,
                     const glm::vec3& halfExtent,
                     const glm::vec4& color = {0.2f, 0.9f, 0.3f, 1.0f});
    void makeWireSphere(const glm::vec3& center,
                        float            radius,
                        const glm::vec4& color = {0.3f, 0.6f, 1.0f, 1.0f});
    void seal();

  private:
    void appendSegment(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);

    bool     bPending     = false;
    uint32_t pendingFirst = 0;
    uint32_t pendingCount = 0;
};

/// World line record. `depthFormat == Undefined` selects the depth-less PSO.
/// The depth image itself is whatever the open render pass attached.
struct WorldDrawTarget
{
    ICommandBuffer* cmd            = nullptr;
    uint32_t        width          = 0;
    uint32_t        height         = 0;
    EFormat::T      colorFormat    = EFormat::Undefined;
    EFormat::T      depthFormat    = EFormat::Undefined;
    glm::mat4       viewProjection = glm::mat4(1.0f);
};

/// Line shader and PSO cache for one IRender. Owned next to `ScreenDrawPipelines`
/// on `RenderDeviceState`. A GUI app does not create this.
struct YA_RENDER_3D_API WorldDrawPipelines
{
    WorldDrawPipelines() = default;
    WorldDrawPipelines(const WorldDrawPipelines&) = delete;
    WorldDrawPipelines& operator=(const WorldDrawPipelines&) = delete;
    WorldDrawPipelines(WorldDrawPipelines&&) = default;
    WorldDrawPipelines& operator=(WorldDrawPipelines&&) = default;

    static constexpr size_t   MaxVertexCount   = 8192;
    static constexpr uint32_t kFrameFlushSlots = 4;

    void init(IRender* render);
    void destroy();
    [[nodiscard]] IGraphicsPipeline* prepare(EFormat::T colorFormat, EFormat::T depthFormat);

    [[nodiscard]] IRender* render() const { return _render; }
    [[nodiscard]] IPipelineLayout* layout() const { return _pipelineLayout.get(); }
    [[nodiscard]] IDescriptorSetLayout* frameLayout() const { return _frameUboDSL.get(); }

    IRender* _render = nullptr;
    std::shared_ptr<IDescriptorSetLayout> _frameUboDSL;
    std::shared_ptr<IPipelineLayout>      _pipelineLayout;

    struct Variant
    {
        EFormat::T                         color = EFormat::Undefined;
        EFormat::T                         depth = EFormat::Undefined;
        std::shared_ptr<IGraphicsPipeline> pipeline;
    };
    std::vector<Variant> _variants;
};

struct YA_RENDER_3D_API WorldDrawRecorder
{
    WorldDrawRecorder() = default;
    WorldDrawRecorder(const WorldDrawRecorder&) = delete;
    WorldDrawRecorder& operator=(const WorldDrawRecorder&) = delete;
    WorldDrawRecorder(WorldDrawRecorder&&) = default;
    WorldDrawRecorder& operator=(WorldDrawRecorder&&) = default;

    struct FrameUBO
    {
        glm::mat4 viewProj = glm::mat4(1.0f);
        glm::mat4 view     = glm::mat4(1.0f);
    };

    void init(WorldDrawPipelines& pipelines);
    void destroy();
    void prepare(EFormat::T colorFormat, EFormat::T depthFormat);
    void record(WorldDrawList& list, const WorldDrawTarget& target);

  private:
    struct Flight
    {
        DescriptorSetHandle      frameUboDS{};
        std::shared_ptr<IBuffer> frameUBOBuffer{};
        std::shared_ptr<IBuffer> vertexBuffer{};
        WorldDrawVertex*         vertexPtrHead = nullptr;
    };

    void ensureResources();

    WorldDrawPipelines* _pipelines = nullptr;
    IRender*            _render    = nullptr;
    IGraphicsPipeline*  _pipeline  = nullptr;
    EFormat::T          _colorFormat = EFormat::Undefined;
    EFormat::T          _depthFormat = EFormat::Undefined;
    std::shared_ptr<IDescriptorPool> _descriptorPool;
    std::array<Flight, MAX_FLIGHTS_IN_FLIGHT> _flights{};
};

} // namespace ya
