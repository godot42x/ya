#pragma once

#include "glm/glm.hpp"

#include "Core/Base.h"
#include "Core/System/System.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Texture.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"
#include "Render/Stage/IRenderStage.h"
#include "Resource/Font/FontManager.h"
#include "Resource/Texture/TextureLibrary.h"
#include "Runtime/Rendering/Common/RenderOverlay.h"

#include <array>

namespace ya
{

enum class ERender2DPassDomain : uint8_t
{
    RuntimeOverlay = 0,
    EditorViewport,
    GameUICompositor,
    EditorCanvas,
    Count,
};

struct FRender2dData
{
    uint32_t        windowWidth      = 800;
    uint32_t        windowHeight     = 600;
    ECullMode::T    screenCullMode   = ECullMode::None;
    ECullMode::T    worldCullMode    = ECullMode::None;
    bool            bReverseViewport = true;
    ICommandBuffer* curCmdBuf        = nullptr;
    bool            bUICompositorMode = false; // flush uses the depth-less UI pipeline
    ERender2DPassDomain passDomain    = ERender2DPassDomain::RuntimeOverlay;

    // Active screen-space clip rects (top-left origin, Y down). The top entry
    // is applied as the scissor on the next screen-batch flush.
    std::vector<Rect2D> clipStack;

    struct Camera
    {
        glm::vec3 position;
        glm::mat4 view;
        glm::mat4 projection;
        glm::mat4 viewProjection;
    } cam;

    int TextLayoutMode = 0;
};

struct FRender2dContext
{
    ICommandBuffer* cmdBuf       = nullptr;
    uint32_t        windowWidth  = 800;
    uint32_t        windowHeight = 600;
    bool            bUICompositorMode = false; // flush uses the depth-less UI pipeline
    ERender2DPassDomain passDomain = ERender2DPassDomain::RuntimeOverlay;

    FRender2dData::Camera cam;
};

struct ENGINE_API FQuadRender
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

    FQuadRender::Vertex* worldVertexPtr     = nullptr;
    FQuadRender::Vertex* worldVertexPtrHead = nullptr;
    uint32_t             worldVertexCount   = 0;
    uint32_t             worldIndexCount    = 0;

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
    std::array<PassPipelines, static_cast<size_t>(ERender2DPassDomain::Count)> _passPipelines{};

    std::shared_ptr<IDescriptorPool> _descriptorPool = nullptr;

    std::shared_ptr<IDescriptorSetLayout> _frameUboDSL = nullptr;

    std::shared_ptr<IDescriptorSetLayout>      _resourceDSL      = nullptr;
    struct FlightResources
    {
        DescriptorSetHandle      frameUboDS{};
        std::shared_ptr<IBuffer> frameUBOBuffer{};
        DescriptorSetHandle      worldFrameUboDS{};
        std::shared_ptr<IBuffer> worldFrameUBOBuffer{};
        DescriptorSetHandle      resourceDS{};
        DescriptorSetHandle      worldResourceDS{};
        std::shared_ptr<IBuffer> vertexBuffer{};
        Vertex*                  vertexPtrHead = nullptr;
        std::shared_ptr<IBuffer> worldVertexBuffer{};
        Vertex*                  worldVertexPtrHead = nullptr;
    };
    struct PassResources
    {
        std::array<FlightResources, MAX_FLIGHTS_IN_FLIGHT> flights{};
    };
    std::array<PassResources, static_cast<size_t>(ERender2DPassDomain::Count)> _passResources{};
    ERender2DPassDomain _activePassDomain = ERender2DPassDomain::RuntimeOverlay;
    uint32_t            _activeFlightIndex = 0;
    uint64_t            _resourceVersion = 0;
    uint64_t            _uploadedScreenResourceVersion = 0;
    uint64_t            _uploadedWorldResourceVersion = 0;
    bool                _frameUboUploaded = false;
    bool                _worldFrameUboUploaded = false;
    std::vector<TextureBinding>                _textureBindings;
    std::unordered_map<std::string, uint32_t>  _textureLabel2Idx;
    static constexpr size_t                    TEXTURE_SET_SIZE     = 16;
    int                                        _lastPushTextureSlot = -1;

    void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    void destroy();
    void begin(const Extent2D& extent);
    void end();
    /// Ensure the depth-less UI compositor pipeline for one pass domain.
    /// Must be called before command recording begins.
    void ensureUICompositorPipeline(ERender2DPassDomain domain, EFormat::T colorFormat);
    /// Ensure a pass domain's regular screen-space pipeline matches its target
    /// attachment formats. Must be called before command recording begins.
    void ensureScreenPipeline(ERender2DPassDomain domain, EFormat::T colorFormat, EFormat::T depthFormat);

    bool shouldFlush() { return vertexCount >= MaxVertexCount - 4 || _lastPushTextureSlot + 1 >= (int)TEXTURE_SET_SIZE; }
    bool shouldFlushWorld() { return worldVertexCount >= MaxVertexCount - 4 || _lastPushTextureSlot + 1 >= (int)TEXTURE_SET_SIZE; }
    void flush(ICommandBuffer* cmdBuf);
    void flushWorld(ICommandBuffer* cmdBuf);

    void updateFrameUBO(std::shared_ptr<IBuffer>& uboBuffer, DescriptorSetHandle dsHandle, const glm::mat4& viewProj, const glm::mat4& view);
    void updateResources(DescriptorSetHandle dsHandle);
    FlightResources& activeFlightResources()
    {
        return _passResources[static_cast<size_t>(_activePassDomain)].flights[_activeFlightIndex];
    }
    PassPipelines& activePassPipelines() { return _passPipelines[static_cast<size_t>(_activePassDomain)]; }

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

    void drawText(const std::string& text, const glm::vec3& position, const glm::vec4& color, Font* font);

  private:
    uint32_t findOrAddTexture(ya::Ptr<Texture> texture)
    {
        uint32_t textureIdx = 0;
        if (texture) {
            auto it = _textureLabel2Idx.find(texture->getLabel());
            if (it != _textureLabel2Idx.end()) {
                textureIdx = it->second;
            }
            else {
                _textureBindings.push_back(TextureBinding{
                    .texture = texture,
                    .sampler = TextureLibrary::get().getDefaultSampler(),
                });
                auto idx                               = static_cast<uint32_t>(_textureBindings.size() - 1);
                _textureLabel2Idx[texture->getLabel()] = idx;
                textureIdx                             = idx;
                _lastPushTextureSlot                   = static_cast<int>(idx);
                ++_resourceVersion;
            }
        }
        return textureIdx;
    }

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
struct ENGINE_API FLineRender
{
    struct Vertex
    {
        glm::vec3 pos;
        glm::vec4 color;
    };

    static constexpr size_t MaxVertexCount = 8192; // 4096 line segments

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
    std::array<PassResources, static_cast<size_t>(ERender2DPassDomain::Count)> _passResources{};
    ERender2DPassDomain _activePassDomain = ERender2DPassDomain::RuntimeOverlay;
    uint32_t            _activeFlightIndex = 0;
    Vertex*             vertexPtr          = nullptr;
    Vertex*             vertexPtrHead      = nullptr;
    uint32_t            vertexCount        = 0;

    void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    void destroy();
    void begin(ERender2DPassDomain domain);
    void flush(ICommandBuffer* cmdBuf, const glm::mat4& viewProj);

    void addLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);
    void addWireBox(const glm::mat4& model, const glm::vec3& halfExtent, const glm::vec4& color);
    void addWireSphere(const glm::vec3& center, float radius, const glm::vec4& color);
};

struct ENGINE_API Render2D
{
    static FQuadRender*  quadData;
    static FLineRender*  lineData;
    static FRender2dData data;

    Render2D()          = default;
    virtual ~Render2D() = default;

    static void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    static void destroy();

    static void onUpdate(float dt);
    static void onRender();

    static void begin(const FRender2dContext& ctx);
    static void end();

    /// Push a clip rect (intersected with the current clip). Changes are applied
    /// as a command-level scissor on the next screen batch flush.
    static void pushClipRect(const Rect2D& rect);
    static void popClipRect();

    /// Lazily create the UI compositor pipeline for the given final-image
    /// color format (depth-less, no post-processing). Must NOT be called while
    /// recording a command buffer.
    static void ensureUICompositorPipeline(ERender2DPassDomain domain, EFormat::T colorFormat);
    static void ensureScreenPipeline(ERender2DPassDomain domain, EFormat::T colorFormat, EFormat::T depthFormat);

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

    static void makeText(const std::string& text, const glm::vec3& position, const glm::vec4& color, Font* font)
    {
        quadData->drawText(text, position, color, font);
    }
};

} // namespace ya
