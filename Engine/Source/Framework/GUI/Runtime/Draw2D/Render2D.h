#pragma once

#include "glm/glm.hpp"

#include "Core/Base.h"

#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/Core/Texture.h"
#include "RHI/RenderDefines.h"

#include <array>

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

/// Diagnostics state adjusted live from the runtime tools panel. These are
/// draw-time parameters only; they are not part of a recording session.
struct FRender2dDebugState
{
    ECullMode::T screenCullMode   = ECullMode::None;
    ECullMode::T worldCullMode    = ECullMode::None;
    bool         bReverseViewport = true;
    int          TextLayoutMode   = 0;
};

/// State of one Render2D recording session, valid between begin()/end().
/// After end() the command buffer is cleared; any draw call outside a session
/// is asserted instead of silently no-op'ing.
struct FRender2dSession
{
    ICommandBuffer*  curCmdBuf   = nullptr;
    uint32_t         windowWidth  = 800;
    uint32_t         windowHeight = 600;
    Render2DPassSlot passSlot     = 0;
    // World-space draw transform (world sprites / debug lines). Screen-space
    // UI never reads these; it uses its own orthographic projection.
    glm::mat4        view          = glm::mat4(1.0f);
    glm::mat4        viewProjection = glm::mat4(1.0f);

    // Active screen-space clip rects (top-left origin, Y down). The top entry
    // is applied as the scissor on the next screen-batch flush.
    std::vector<Rect2D> clipStack;
};

struct FRender2dContext
{
    ICommandBuffer*  cmdBuf       = nullptr;
    uint32_t         windowWidth  = 800;
    uint32_t         windowHeight = 600;
    Render2DPassSlot passSlot     = 0;
    glm::mat4        view          = glm::mat4(1.0f);
    glm::mat4        viewProjection = glm::mat4(1.0f);
};

struct YA_GUI_API FQuadRender
{
    struct Vertex
    {
        glm::vec3 pos;
        glm::vec4 color;
        glm::vec2 texCoord;
        uint32_t  textureIdx;
        glm::vec3 worldCenter;
        glm::vec3 worldDirection;
        glm::vec2 worldSize;
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
    static constexpr uint32_t kMaxPassSlots = 8;

    // One host-visible vertex buffer is shared by every flush of a frame, and
    // the GPU only reads it after the whole command buffer is recorded. Each
    // flush must therefore write to a DISTINCT region (advancing cursor), or
    // later flushes would overwrite earlier batches before the GPU executes.
    // The buffer holds up to this many full batches per frame; exceeding it
    // is a hard error (fail loudly instead of silently dropping content).
    static constexpr uint32_t kFrameFlushSlots = 4;

    struct FrameUBO
    {
        glm::mat4 viewProj = glm::mat4(1.0f);
        glm::mat4 view     = glm::mat4(1.0f);
    };

    IRender* _render = nullptr;

    glm::mat4 _screenOrthoProj = glm::mat4(1.0f);

    std::shared_ptr<IBuffer> _indexBuffer;

    FQuadRender::Vertex* vertexPtr     = nullptr;
    FQuadRender::Vertex* vertexPtrHead = nullptr;
    uint32_t             vertexCount   = 0;
    uint32_t             indexCount    = 0;
    uint32_t             screenBatchStartVertex = 0; // start of the pending batch in the shared buffer

    FQuadRender::Vertex* worldVertexPtr     = nullptr;
    FQuadRender::Vertex* worldVertexPtrHead = nullptr;
    uint32_t             worldVertexCount   = 0;
    uint32_t             worldIndexCount    = 0;
    uint32_t             worldBatchStartVertex = 0; // start of the pending world batch

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
                        .stageFlags      = EShaderStage::Vertex,
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
    std::shared_ptr<IGraphicsPipeline> _worldPipeline  = nullptr;
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
        DescriptorSetHandle      worldFrameUboDS{};
        std::shared_ptr<IBuffer> worldFrameUBOBuffer{};
        std::vector<DescriptorSetHandle> screenResourceDSPool{};
        std::vector<DescriptorSetHandle> worldResourceDSPool{};
        uint32_t                         nextScreenResourceDS = 0;
        uint32_t                         nextWorldResourceDS  = 0;
        DescriptorSetHandle              activeScreenResourceDS{};
        DescriptorSetHandle              activeWorldResourceDS{};
        std::shared_ptr<IBuffer> vertexBuffer{};
        Vertex*                  vertexPtrHead = nullptr;
        std::shared_ptr<IBuffer> worldVertexBuffer{};
        Vertex*                  worldVertexPtrHead = nullptr;
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
    uint64_t            _uploadedWorldResourceVersion = 0;
    bool                _frameUboUploaded = false;
    bool                _worldFrameUboUploaded = false;
    std::vector<TextureBinding>                _textureBindings;
    std::unordered_map<std::string, uint32_t>  _textureLabel2Idx;
    static constexpr size_t                    TEXTURE_SET_SIZE     = 16;
    static constexpr uint32_t                  RESOURCE_DS_POOL_SIZE = 64;
    int                                        _lastPushTextureSlot = -1;

    void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    void destroy();
    /// Lazily allocate one pass slot's buffers + descriptor sets (all flights).
    void ensureSlotResources(Render2DPassSlot passSlot);
    void begin(Render2DPassSlot passSlot, const Extent2D& extent);
    void end();
    /// Ensure a pass slot's screen-space pipeline matches its target attachment
    /// formats. A depth-less target (depthFormat == Undefined) resolves to the
    /// depth-less UI variant. Must be called before command recording begins.
    void preparePassPipeline(Render2DPassSlot passSlot, EFormat::T colorFormat, EFormat::T depthFormat);

    bool shouldFlush() { return vertexCount >= MaxVertexCount - 4 || _lastPushTextureSlot + 1 >= (int)TEXTURE_SET_SIZE; }
    bool shouldFlushWorld() { return worldVertexCount >= MaxVertexCount - 4 || _lastPushTextureSlot + 1 >= (int)TEXTURE_SET_SIZE; }
    void flush(ICommandBuffer* cmdBuf);
    void flushWorld(ICommandBuffer* cmdBuf);
    void resetTextureBatch();
    void flushForTextureOverflow(ICommandBuffer* cmdBuf);

    void updateFrameUBO(std::shared_ptr<IBuffer>& uboBuffer, DescriptorSetHandle dsHandle, const glm::mat4& viewProj, const glm::mat4& view);
    void updateResources(DescriptorSetHandle dsHandle);
    DescriptorSetHandle acquireScreenResourceDS(FlightResources& resources);
    DescriptorSetHandle acquireWorldResourceDS(FlightResources& resources);
    FlightResources& activeFlightResources()
    {
        return _passResources[static_cast<size_t>(_activePassSlot)].flights[_activeFlightIndex];
    }
    PassPipelines& activePassPipelines() { return _passPipelines[static_cast<size_t>(_activePassSlot)]; }

  public:
    void drawTexture(const glm::vec3& position,
                     const glm::vec2& size,
                     ya::Ptr<Texture> texture = nullptr,
                     const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                     const glm::vec2& uvScale = {1.0f, 1.0f});

    void drawTexture(const glm::mat4& transform,
                     ya::Ptr<Texture> texture = nullptr,
                     const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                     const glm::vec2& uvScale = {1.0f, 1.0f});

    void drawWorldTexture(const glm::vec3& center,
                          const glm::vec3& direction,
                          const glm::vec2& size,
                          ya::Ptr<Texture> texture = nullptr,
                          const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                          const glm::vec2& uvScale = {1.0f, 1.0f});

    void drawSubTexture(const glm::vec3& position,
                        const glm::vec2& size,
                        ya::Ptr<Texture> texture = nullptr,
                        const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                        const glm::vec4& uvRect  = glm::vec4(0.0f));

    void drawText(const std::string& text,
                  const glm::vec3&   position,
                  const glm::vec4&   color,
                  Font*              font,
                  const glm::vec2&   scale = glm::vec2(1.0f));

  private:
    uint32_t findOrAddTexture(ya::Ptr<Texture> texture);

    void drawTextureInternal(const glm::mat4& transform,
                             uint32_t textureIdx,
                             const glm::vec3 tint,
                             const glm::vec2& uvScale,
                             const glm::vec2& uvTranslation = {0, 0});

    void drawWorldTextureInternal(const glm::vec3& center,
                                  const glm::vec3& direction,
                                  const glm::vec2& size,
                                  uint32_t textureIdx,
                                  const glm::vec3 tint,
                                  const glm::vec2& uvScale);
};

/**
 * @brief FLineRender - World-space debug line rendering used by Render2D.
 *
 * A minimal line-list pipeline sharing the camera view-projection convention
 * of the world-space sprite pipeline. Used for debug overlays such as
 * physics collision boxes.
 */
struct YA_GUI_API FLineRender
{
    struct Vertex
    {
        glm::vec3 pos;
        glm::vec4 color;
    };

    static constexpr size_t MaxVertexCount = 8192; // 4096 line segments

    // Same shared-buffer rule as FQuadRender: multiple flushes per frame must
    // write to distinct regions because the GPU reads the buffer only after
    // recording finishes.
    static constexpr uint32_t kFrameFlushSlots = 4;

    using FrameUBO = FQuadRender::FrameUBO;

    IRender* _render = nullptr;

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
                        .stageFlags      = EShaderStage::Vertex,
                    },
                },
            },
        },
    };

    std::shared_ptr<IDescriptorPool>      _descriptorPool;
    std::shared_ptr<IDescriptorSetLayout> _frameUboDSL;
    std::shared_ptr<IPipelineLayout>      _pipelineLayout;
    std::shared_ptr<IGraphicsPipeline>    _pipeline;

    struct FlightResources
    {
        DescriptorSetHandle      frameUboDS{};
        std::shared_ptr<IBuffer> frameUBOBuffer{};
        std::shared_ptr<IBuffer> vertexBuffer{};
        Vertex*                  vertexPtrHead = nullptr;
    };
    struct PassResources
    {
        std::array<FlightResources, MAX_FLIGHTS_IN_FLIGHT> flights{};
    };
    std::array<PassResources, FQuadRender::kMaxPassSlots> _passResources{};
    Render2DPassSlot _activePassSlot = 0;
    uint32_t         _activeFlightIndex = 0;
    Vertex*          vertexPtr          = nullptr;
    Vertex*          vertexPtrHead      = nullptr;
    uint32_t         vertexCount        = 0;
    uint32_t         batchStartVertex   = 0; // start of the pending batch in the shared buffer

    void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    void destroy();
    /// Lazily allocate one pass slot's buffers + descriptor sets (all flights).
    void ensureSlotResources(Render2DPassSlot passSlot);
    void begin(Render2DPassSlot passSlot);
    void flush(ICommandBuffer* cmdBuf, const glm::mat4& viewProj);

    void addLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);
    void addWireBox(const glm::mat4& model, const glm::vec3& halfExtent, const glm::vec4& color);
    void addWireSphere(const glm::vec3& center, float radius, const glm::vec4& color);
};

struct YA_GUI_API Render2D
{
    static FQuadRender*  quadData;
    static FLineRender*  lineData;
    static FRender2dDebugState debug;
    static FRender2dSession    session;

    Render2D()          = default;
    virtual ~Render2D() = default;

    static void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    static void destroy();

    static void onUpdate(float dt);
    static void onRender();

    static void begin(const FRender2dContext& ctx);
    static void end();

    /// Acquire a unique pass slot for this Render2D instance. Call once at
    /// setup; the returned index is caller-owned and maps the caller's own
    /// pass vocabulary onto Render2D's per-pass resources.
    [[nodiscard]] static Render2DPassSlot acquirePassSlot();

    /// Push a clip rect (intersected with the current clip). Changes are applied
    /// as a command-level scissor on the next screen batch flush.
    static void pushClipRect(const Rect2D& rect);
    static void popClipRect();

    /// Pure clip intersection used by the clip stack: `rect` clipped to the
    /// current `parentClip` (empty extent when disjoint). Extracted so the
    /// nested-clip semantics are unit-testable without a render session.
    [[nodiscard]] static Rect2D intersectClipRect(const Rect2D& rect, const Rect2D& parentClip);

    /// Lazily create the screen-space pipeline variant required by one pass
    /// slot. A depth-less target (depthFormat == Undefined) uses the depth-less
    /// UI variant; a depth-attached target uses the depth-aware screen variant.
    /// Must NOT be called while recording a command buffer.
    static void preparePassPipeline(Render2DPassSlot passSlot, EFormat::T colorFormat, EFormat::T depthFormat);

    static void makeSprite(const glm::vec3& position,
                           const glm::vec2& size,
                           ya::Ptr<Texture> texture = nullptr,
                           const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                           const glm::vec2& uvScale = {1.0f, 1.0f})
    {
        quadData->drawTexture(position, size, texture, tint, uvScale);
    }

    static void makeSprite(const glm::mat4& transform,
                           ya::Ptr<Texture> texture = nullptr,
                           const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                           const glm::vec2& uvScale = {1.0f, 1.0f})
    {
        quadData->drawTexture(transform, texture, tint, uvScale);
    }

    static void makeWorldSprite(const glm::vec3& worldCenter,
                                const glm::vec3& worldDirection,
                                const glm::vec2& worldSize,
                                ya::Ptr<Texture> texture = nullptr,
                                const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                                const glm::vec2& uvScale = {1.0f, 1.0f})
    {
        quadData->drawWorldTexture(worldCenter, worldDirection, worldSize, texture, tint, uvScale);
    }

    static void makeWorldLine(const glm::vec3& from,
                              const glm::vec3& to,
                              const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f})
    {
        lineData->addLine(from, to, color);
    }

    static void makeWireBox(const glm::mat4& model,
                            const glm::vec3& halfExtent,
                            const glm::vec4& color = {0.2f, 0.9f, 0.3f, 1.0f})
    {
        lineData->addWireBox(model, halfExtent, color);
    }

    static void makeWireSphere(const glm::vec3& center,
                               float            radius,
                               const glm::vec4& color = {0.3f, 0.6f, 1.0f, 1.0f})
    {
        lineData->addWireSphere(center, radius, color);
    }

    static void makeText(const std::string& text,
                         const glm::vec3&   position,
                         const glm::vec4&   color,
                         Font*              font,
                         const glm::vec2&   scale = glm::vec2(1.0f))
    {
        quadData->drawText(text, position, color, font, scale);
    }
};

} // namespace ya
