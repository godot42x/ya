#pragma once

#include "glm/glm.hpp"

#include "Core/Base.h"
#include "Core/Common/Types.h"

#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/Core/Texture.h"
#include "RHI/RenderDefines.h"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

struct IRender;
struct Font;

/// Opaque pass slot: Render2D keeps per-pass GPU resources (vertex buffers,
/// descriptor sets, screen pipelines) isolated so several passes can record
/// into one command buffer without descriptor invalidation. Callers acquire a
/// slot once at setup and map their own pass vocabulary (runtime overlay, UI
/// composite, editor viewport, ...) onto the returned index; Render2D itself
/// does not know about game/editor passes.
using Render2DPassSlot = uint32_t;
inline constexpr Render2DPassSlot kInvalidRender2DPassSlot = ~Render2DPassSlot{0};

/// Screen-space quad vertex for the GUI compose shader
/// (Sprite2DScreen.slang): a transformed quad with typed texture draw data.
/// No world payload -- world billboards live in the scene-side 2D path.
struct ScreenVertex
{
    glm::vec3 pos;
    glm::vec4 color;
    glm::vec2 texCoord;
    /// Typed draw data: descriptor-array slot and sampling mode are separate
    /// fields (no bit packing). The same atlas is sampled Coverage or Sdf
    /// depending on glyph size, and Opaque for targets that leave alpha
    /// unused -- so the mode rides per quad next to the slot.
    uint32_t  textureSlot;
    uint32_t  sampleMode;
    // Rounded-rect SDF params. cornerRadius = corner radius in target px;
    // quadSize = quad size in target px. A positive cornerRadius routes the
    // fragment shader into the SDF round-rect alpha branch (no texture needed).
    glm::vec3 corner;
};

;

struct YA_RENDER_2D_API FQuadRender
{
    enum class ETextureSampleMode : uint8_t
    {
        Coverage = 0,
        Sdf      = 1,
        Opaque   = 2,
    };

    static constexpr const std::array<glm::vec4, 4> vertices        = {{
        {0.0f, 0.0f, 0.0f, 1.f},
        {1.0f, 0.0f, 0.0f, 1.f},
        {0.0f, 1.0f, 0.0f, 1.f},
        {1.0f, 1.0f, 0.0f, 1.f},
    }};
    static constexpr const std::array<glm::vec2, 4> defaultTexcoord = {{
        {0, 0},
        {1, 0},
        {0, 1},
        {1, 1},
    }};

    static constexpr size_t MaxVertexCount = 10000;
    static constexpr size_t MaxIndexCount  = MaxVertexCount * 6 / 4;

    // Upper bound on concurrently used pass slots (see Render2DPassSlot).
    // Per-slot resources are allocated lazily on first use, so a GUI app that
    // uses one slot only allocates one slot's buffers.
    static constexpr uint32_t kMaxPassSlots = 16;

    // One host-visible vertex buffer is shared by every flush of a frame, and
    // the GPU only reads it after the whole command buffer is recorded. Each
    // flush must therefore write to a DISTINCT region (advancing cursor), or
    // later flushes would overwrite earlier batches before the GPU executes.
    // The buffer holds up to this many full batches per frame; exceeding it
    // is a hard error (fail loudly instead of silently dropping content).
    static constexpr uint32_t kFrameFlushSlots = 4;

    struct FrameUBO
    {
        glm::mat4  viewProj = glm::mat4(1.0f);
        glm::mat4  view     = glm::mat4(1.0f);
    };

    /// GPU counters from one recorded 2D list. The record step accumulates
    /// them; `Render2D::lastFrameStats()` surfaces the most recent.
    struct FRender2dFrameStats
    {
        uint32_t screenFlushCount  = 0;
        uint32_t screenVertexCount = 0;
        uint32_t screenIndexCount  = 0;
    };

    /// Everything a flush needs from the recording, passed in instead of read
    /// from a global session: the record step resolves it once and updates the
    /// clip per command. `stats`/the debug log counters are owned by the
    /// record step and accumulate across its flushes.
    struct FRender2dFlushState
    {
        uint32_t  windowWidth  = 0;
        uint32_t  windowHeight = 0;
        bool      bClipped     = false;
        Rect2D    clip{};
        glm::mat4 view           = glm::mat4(1.0f);
        glm::mat4 viewProjection = glm::mat4(1.0f);
        FRender2dFrameStats* stats               = nullptr;
        uint32_t*            debugScreenFlushCount = nullptr;
    };

    IRender* _render = nullptr;

    glm::mat4 _screenOrthoProj = glm::mat4(1.0f);

    std::shared_ptr<IBuffer> _indexBuffer;

    ScreenVertex* vertexPtr     = nullptr;
    ScreenVertex* vertexPtrHead = nullptr;
    uint32_t             vertexCount   = 0;
    uint32_t             indexCount    = 0;
    uint32_t             screenBatchStartVertex = 0; // start of the pending batch in the shared buffer

    PipelineLayoutDesc _pipelineDesc = PipelineLayoutDesc{
        .pushConstants        = {},
        .descriptorSetLayouts = {
            DescriptorSetLayoutDesc{
                .label    = "Frame_UBO",
                .set      = 0,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment,
                    },
                },
            },
            DescriptorSetLayoutDesc{
                .label    = "CombinedImageSampler",
                .set      = 0,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = TEXTURE_SET_SIZE,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
        },
    };

    std::shared_ptr<IPipelineLayout>   _pipelineLayout = nullptr;
    struct PassPipelines
    {
        std::shared_ptr<IGraphicsPipeline> screenPipeline{};
        EFormat::T                         screenColorFormat = EFormat::Undefined;
        EFormat::T                         screenDepthFormat = EFormat::Undefined;
        std::shared_ptr<IGraphicsPipeline> uiPipeline{};
        EFormat::T                         uiColorFormat = EFormat::Undefined;
    };
    std::array<PassPipelines, kMaxPassSlots> _passPipelines{};

    std::shared_ptr<IDescriptorPool> _descriptorPool = nullptr;

    std::shared_ptr<IDescriptorSetLayout> _frameUboDSL = nullptr;

    std::shared_ptr<IDescriptorSetLayout>      _resourceDSL      = nullptr;
    struct FlightResources
    {
        DescriptorSetHandle      frameUboDS{};
        std::shared_ptr<IBuffer> frameUBOBuffer{};
        std::vector<DescriptorSetHandle> screenResourceDSPool{};
        uint32_t                         nextScreenResourceDS = 0;
        DescriptorSetHandle              activeScreenResourceDS{};
        std::shared_ptr<IBuffer> vertexBuffer{};
        ScreenVertex*            vertexPtrHead = nullptr;
    };
    struct PassResources
    {
        std::array<FlightResources, MAX_FLIGHTS_IN_FLIGHT> flights{};
    };
    std::array<PassResources, kMaxPassSlots> _passResources{};
    Render2DPassSlot _activePassSlot = 0;
    uint32_t         _activeFlightIndex = 0;
    uint64_t            _resourceVersion = 0;
    uint64_t            _uploadedScreenResourceVersion = 0;
    bool                _frameUboUploaded = false;
    std::vector<TextureBinding>                _textureBindings;
    std::unordered_map<const Texture*, uint32_t> _texturePtr2Idx;
    // The packed GPU representation keeps texture slot and sampling semantics
    // together per draw, without making callers manipulate bit flags.
    static constexpr size_t                    TEXTURE_SET_SIZE     = 16;
    static constexpr uint32_t                  RESOURCE_DS_POOL_SIZE = 64;
    int                                        _lastPushTextureSlot = -1;

    void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    void destroy();
    /// Lazily allocate one pass slot's buffers + descriptor sets (all flights).
    void ensureSlotResources(Render2DPassSlot passSlot);
    /// `flightSlot` is the slot of this pass's per-frame ring this recording may
    /// use; `Render2D::begin` resolves it from the device's frames in flight
    /// (see `IRender::framesInFlight`). Render2D never asks a swapchain which
    /// frame it is on -- that was the primary window leaking into 2D batching.
    void begin(Render2DPassSlot passSlot, const Extent2D& extent, uint32_t flightSlot);
    void end();
    /// Ensure a pass slot's screen-space pipeline matches its target attachment
    /// formats. A depth-less target (depthFormat == Undefined) resolves to the
    /// depth-less UI variant; a depth-attached target uses the depth-aware
    /// screen variant. Must be called before command recording begins.
    void preparePassPipeline(Render2DPassSlot passSlot, EFormat::T colorFormat, EFormat::T depthFormat);

    bool shouldFlush() { return vertexCount >= MaxVertexCount - 4 || _lastPushTextureSlot + 1 >= (int)TEXTURE_SET_SIZE; }
    void flush(ICommandBuffer* cmdBuf, const FRender2dFlushState& state);
    void resetTextureBatch();
    /// Whether one more texture would overflow the per-record binding table
    /// (the record step checks this before registering list textures and
    /// flushes + resets instead of relying on the lazy overflow path).
    [[nodiscard]] bool textureTableFull() const { return _textureBindings.size() >= TEXTURE_SET_SIZE; }

    void updateFrameUBO(std::shared_ptr<IBuffer>& uboBuffer, const glm::mat4& viewProj, const glm::mat4& view);
    void updateResources(DescriptorSetHandle dsHandle);
    DescriptorSetHandle acquireScreenResourceDS(FlightResources& resources);
    FlightResources& activeFlightResources()
    {
        return _passResources[static_cast<size_t>(_activePassSlot)].flights[_activeFlightIndex];
    }
    PassPipelines& activePassPipelines() { return _passPipelines[static_cast<size_t>(_activePassSlot)]; }

    /// Pure CPU vertex emit, shared by the immediate draw path and the
    /// Render2DList builder: `out` receives exactly 4 vertices. No state, no
    /// GPU -- the destination decides where the vertices live.
    static void EmitScreenQuad(ScreenVertex*                   out,
                               const glm::mat4&                transform,
                               uint32_t                        textureSlot,
                               uint32_t                        sampleMode,
                               const std::array<glm::vec4, 4>& colorsYaOrder,
                               const glm::vec2&                uvScale,
                               const glm::vec2&                uvTranslation,
                               const glm::vec3&                corner);

    /// Resolve (or lazily add) a texture's slot in the per-record binding
    /// table. Public because the record step registers list textures through
    /// it; the caller guarantees room (`textureTableFull()`) so the lazy
    /// overflow flush inside never fires on the list path.
    [[nodiscard]] uint32_t findOrAddTexture(ya::Ptr<Texture> texture);

};

} // namespace ya
