#pragma once

#include "Render2D/ScreenDrawList.h"

#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/Core/Texture.h"
#include "RHI/RenderDefines.h"

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ya
{

struct IRender;
struct ICommandBuffer;

/// Draw-time diagnostics. Set before recording; not part of a list.
struct ScreenDrawDiagnostics
{
    ECullMode::T screenCullMode       = ECullMode::None;
    bool         bLogSessionLifecycle = false;
    bool         bLogClipStack        = false;
    bool         bLogFlushBatches     = false;
    uint32_t     maxClipLogsPerFrame  = 16;
    uint32_t     maxFlushLogsPerFrame = 16;
};

[[nodiscard]] YA_RENDER_2D_API ScreenDrawDiagnostics& screenDrawDiagnostics();

inline constexpr uint32_t kScreenTextureSetSize = 16;

/// What one screen record draws into. The pipeline for `colorFormat` must
/// already have been prepared on the recorder. No camera: a screen list does
/// not have one.
struct ScreenDrawTarget
{
    ICommandBuffer* cmd         = nullptr;
    uint32_t        width       = 0;
    uint32_t        height      = 0;
    EFormat::T      colorFormat = EFormat::Undefined;
};

/// Shader, layouts, static index buffer, and the PSO cache for one IRender.
/// Keyed by (color format, depth format). Owned by whoever creates the device:
/// `GUIAppHost` for a GUI app, `RenderDeviceState` for `ya::App`.
struct YA_RENDER_2D_API ScreenDrawPipelines
{
    ScreenDrawPipelines() = default;
    ScreenDrawPipelines(const ScreenDrawPipelines&) = delete;
    ScreenDrawPipelines& operator=(const ScreenDrawPipelines&) = delete;
    ScreenDrawPipelines(ScreenDrawPipelines&&) = default;
    ScreenDrawPipelines& operator=(ScreenDrawPipelines&&) = default;

    static constexpr size_t   MaxVertexCount = 10000;
    /// A batch is a triangle list. Three indices per vertex covers a fan and
    /// a feathered stroke; quads use fewer.
    static constexpr size_t   MaxIndexCount  = MaxVertexCount * 3;
    static constexpr uint32_t kFrameFlushSlots = 4;

    void init(IRender* render);
    void destroy();

    /// Lazily create the PSO for this attachment pair. Must not run while a
    /// command buffer is being recorded.
    [[nodiscard]] IGraphicsPipeline* prepare(EFormat::T colorFormat, EFormat::T depthFormat);

    [[nodiscard]] IRender* render() const { return _render; }
    [[nodiscard]] IPipelineLayout* layout() const { return _pipelineLayout.get(); }
    [[nodiscard]] IDescriptorSetLayout* frameLayout() const { return _frameUboDSL.get(); }
    [[nodiscard]] IDescriptorSetLayout* resourceLayout() const { return _resourceDSL.get(); }

    IRender* _render = nullptr;

    std::shared_ptr<IDescriptorSetLayout> _frameUboDSL;
    std::shared_ptr<IDescriptorSetLayout> _resourceDSL;
    std::shared_ptr<IPipelineLayout>      _pipelineLayout;

    struct Variant
    {
        EFormat::T                         color = EFormat::Undefined;
        EFormat::T                         depth = EFormat::Undefined;
        std::shared_ptr<IGraphicsPipeline> pipeline;
    };
    std::vector<Variant> _variants;
};

/// Upload rings, frame UBO, and descriptor sets for one draw target. Replaces
/// a pass slot: two targets that record into the same command buffer each
/// hold a recorder.
struct YA_RENDER_2D_API ScreenDrawRecorder
{
    ScreenDrawRecorder() = default;
    ScreenDrawRecorder(const ScreenDrawRecorder&) = delete;
    ScreenDrawRecorder& operator=(const ScreenDrawRecorder&) = delete;
    ScreenDrawRecorder(ScreenDrawRecorder&&) = default;
    ScreenDrawRecorder& operator=(ScreenDrawRecorder&&) = default;

    static constexpr uint32_t kResourceDescriptorSets = 64;

    struct FrameUBO
    {
        glm::mat4 viewProj = glm::mat4(1.0f);
        glm::mat4 view     = glm::mat4(1.0f);
    };

    void init(ScreenDrawPipelines& pipelines);
    void destroy();

    /// Select the already-cached PSO for this target. Same rule as
    /// `ScreenDrawPipelines::prepare`: not during command recording.
    void prepare(EFormat::T colorFormat, EFormat::T depthFormat);

    [[nodiscard]] ScreenDrawFrameStats record(ScreenDrawList& list, const ScreenDrawTarget& target);

    [[nodiscard]] bool textureTableFull() const { return _textureBindings.size() >= kScreenTextureSetSize; }

  private:
    struct Flight
    {
        DescriptorSetHandle              frameUboDS{};
        std::shared_ptr<IBuffer>         frameUBOBuffer{};
        std::vector<DescriptorSetHandle> resourceDSPool{};
        uint32_t                         nextResourceDS = 0;
        DescriptorSetHandle              activeResourceDS{};
        std::shared_ptr<IBuffer>         vertexBuffer{};
        ScreenVertex*                    vertexPtrHead = nullptr;
        std::shared_ptr<IBuffer>         indexBuffer{};
        uint32_t*                        indexPtrHead = nullptr;
    };

    void ensureResources();
    void begin(const Extent2D& extent, uint32_t flightSlot);
    void flush(ICommandBuffer* cmdBuf, uint32_t width, uint32_t height, bool bClipped, const Rect2D& clip, ScreenDrawFrameStats* stats);
    void resetTextureBatch();
    void updateResources(DescriptorSetHandle dsHandle);
    [[nodiscard]] DescriptorSetHandle acquireResourceDS(Flight& flight);
    [[nodiscard]] uint32_t findOrAddTexture(ya::Ptr<Texture> texture);
    [[nodiscard]] Flight& activeFlight() { return _flights[_activeFlightIndex]; }

    ScreenDrawPipelines* _pipelines = nullptr;
    IRender*             _render    = nullptr;
    IGraphicsPipeline*   _pipeline  = nullptr;
    EFormat::T           _colorFormat = EFormat::Undefined;
    EFormat::T           _depthFormat = EFormat::Undefined;

    std::shared_ptr<IDescriptorPool> _descriptorPool;
    std::array<Flight, MAX_FLIGHTS_IN_FLIGHT> _flights{};
    uint32_t _activeFlightIndex = 0;

    glm::mat4     _screenOrthoProj = glm::mat4(1.0f);
    ScreenVertex* _vertexPtr        = nullptr;
    ScreenVertex* _vertexPtrHead    = nullptr;
    uint32_t*     _indexPtr         = nullptr;
    uint32_t      _vertexCount      = 0;
    uint32_t      _indexCount       = 0;
    uint32_t      _batchStartVertex = 0;
    uint32_t      _batchStartIndex  = 0;
    uint64_t      _resourceVersion = 0;
    uint64_t      _uploadedResourceVersion = 0;
    bool          _frameUboUploaded = false;
    std::vector<TextureBinding> _textureBindings;
    std::unordered_map<const Texture*, uint32_t> _texturePtr2Idx;
    int _lastPushTextureSlot = -1;
};

} // namespace ya
