
#pragma once

#include "glm/glm.hpp"

#include "Core/Base.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Texture.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"
#include "Resource/TextureLibrary.h"


#include "Core/System/System.h"


#include "Resource/FontManager.h"

namespace ya
{



struct FRender2dData
{
    // std::vector<stdptr<RenderSystem>> _systems;

    uint32_t        windowWidth      = 800;
    uint32_t        windowHeight     = 600;
    ECullMode::T    screenCullMode   = ECullMode::Back;  // debug: world-space cull mode (Front when viewport reversed)
    ECullMode::T    worldCullMode    = ECullMode::Front; // debug: world-space cull mode (Front when viewport reversed)
    bool            bReverseViewport = true; // flip viewport Y for world-space path to unify LH coordinate system across render backends
    ICommandBuffer* curCmdBuf        = nullptr;

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
    // std::vector<stdptr<RenderSystem>> _systems;

    ICommandBuffer* cmdBuf        = nullptr;
    uint32_t        windowWidth      = 800;
    uint32_t        windowHeight     = 600;

    FRender2dData::Camera cam;
};


struct FQuadRender
{
    struct Vertex
    {
        glm::vec3 pos;
        glm::vec4 color;
        glm::vec2 texCoord;
        uint32_t  textureIdx;
    };


    static constexpr const std::array<glm::vec4, 4> vertices        = {{
        {0.0f, 0.0f, 0.0f, 1.f}, // LT
        {1.0f, 0.0f, 0.0f, 1.f}, // RT
        {0.0f, 1.0f, 0.0f, 1.f}, // LB
        {1.0f, 1.0f, 0.0f, 1.f}, // RB
    }};
    static constexpr const std::array<glm::vec2, 4> defaultTexcoord = {{
        {0, 0}, // LT
        {1, 0}, // RT
        {0, 1}, // LB
        {1, 1}, // RB
    }};

    static constexpr size_t MaxVertexCount = 10000;
    static constexpr size_t MaxIndexCount  = MaxVertexCount * 6 / 4; // 6 indices per quad, 4 vertices per quad

    // static constexpr

    struct FrameUBO
    {
        glm::mat4 viewProj = glm::mat4(1.0f);
    };

    IRender* _render = nullptr;

    glm::mat4 _screenOrthoProj = glm::mat4(1.0f); // cached ortho projection for screen-space flush

    std::shared_ptr<IBuffer> _vertexBuffer;
    std::shared_ptr<IBuffer> _indexBuffer;

    // Screen-space batch
    FQuadRender::Vertex* vertexPtr     = nullptr;
    FQuadRender::Vertex* vertexPtrHead = nullptr;
    uint32_t vertexCount = 0;
    uint32_t indexCount  = 0;

    // World-space batch
    std::shared_ptr<IBuffer> _worldVertexBuffer;
    FQuadRender::Vertex*     worldVertexPtr     = nullptr;
    FQuadRender::Vertex*     worldVertexPtrHead = nullptr;
    uint32_t                 worldVertexCount   = 0;
    uint32_t                 worldIndexCount    = 0;


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
    std::shared_ptr<IGraphicsPipeline> _pipeline       = nullptr; // screen-space pipeline
    std::shared_ptr<IGraphicsPipeline> _worldPipeline  = nullptr; // world-space pipeline

    // descriptor set layout & pool
    std::shared_ptr<IDescriptorPool> _descriptorPool = nullptr;

    std::shared_ptr<IDescriptorSetLayout> _frameUboDSL    = nullptr;

    // Screen-space FrameUBO
    DescriptorSetHandle                   _frameUboDS     = {};
    std::shared_ptr<IBuffer>              _frameUBOBuffer = nullptr;

    // World-space FrameUBO (separate buffer to avoid GPU read hazard)
    DescriptorSetHandle                   _worldFrameUboDS     = {};
    std::shared_ptr<IBuffer>              _worldFrameUBOBuffer = nullptr;

    std::shared_ptr<IDescriptorSetLayout>     _resourceDSL = nullptr;
    DescriptorSetHandle                       _resourceDS  = {};
    std::vector<TextureBinding>                  _textureBindings;
    std::unordered_map<std::string, uint32_t> _textureLabel2Idx;
    static constexpr size_t                   TEXTURE_SET_SIZE     = 32;
    int                                       _lastPushTextureSlot = -1;

    // Note: White texture and default sampler are now provided by TextureLibrary

    void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    void destroy();

    void onImGui()
    {
        // control viewports &scissors
    }

    void begin(const Extent2D& extent);
    void end();

    bool shouldFlush() { return vertexCount >= MaxVertexCount - 4 || _lastPushTextureSlot + 1 >= (int)TEXTURE_SET_SIZE; }
    bool shouldFlushWorld() { return worldVertexCount >= MaxVertexCount - 4 || _lastPushTextureSlot + 1 >= (int)TEXTURE_SET_SIZE; }
    void flush(ICommandBuffer* cmdBuf);
    void flushWorld(ICommandBuffer* cmdBuf);

    void updateFrameUBO(std::shared_ptr<IBuffer>& uboBuffer, DescriptorSetHandle dsHandle, glm::mat4 viewProj);
    void updateResources();

  public:

    // on screen
    void drawTexture(const glm::vec3& position,
                     const glm::vec2& size,
                     ya::Ptr<Texture> texture = nullptr,
                     const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                     const glm::vec2& uvScale = {1.0f, 1.0f});

    void drawTexture(const glm::mat4& transform,
                     ya::Ptr<Texture> texture = nullptr,
                     const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                     const glm::vec2& uvScale = {1.0f, 1.0f});

    // World-space draw
    void drawWorldTexture(const glm::mat4& transform,
                          ya::Ptr<Texture> texture = nullptr,
                          const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                          const glm::vec2& uvScale = {1.0f, 1.0f});

    void drawSubTexture(const glm::vec3& position,
                        const glm::vec2& size,
                        ya::Ptr<Texture> texture = nullptr,
                        const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                        const glm::vec4& uvRect  = glm::vec4(0.0f) // offset: xy , scale: zw
    );

    void drawText(const std::string& text, const glm::vec3& position, const glm::vec4& color, Font* font);

  private:

    uint32_t findOrAddTexture(ya::Ptr<Texture> texture)
    {
        uint32_t textureIdx = 0; // white texture
        if (texture) {
            // TODO: use ptr as key?
            auto it = _textureLabel2Idx.find(texture->getLabel());
            if (it != _textureLabel2Idx.end())
            {
                textureIdx = it->second;
            }
            else {
                // TODO: use map to cache same texture view
                // TODO: different sampler?
                _textureBindings.push_back(TextureBinding{
                    .texture = texture,
                    .sampler = TextureLibrary::get().getDefaultSampler(),
                });
                auto idx                               = static_cast<uint32_t>(_textureBindings.size() - 1);
                _textureLabel2Idx[texture->getLabel()] = idx;
                textureIdx                             = idx;
                _lastPushTextureSlot                   = static_cast<int>(idx);
            }
        }
        return textureIdx;
    }
    void drawTextureInternal(const glm::mat4& transform,
                             uint32_t textureIdx, const glm::vec3 tint,
                             const glm::vec2& uvScale,
                             const glm::vec2& uvTranslation = {0, 0});

    void drawWorldTextureInternal(const glm::mat4& transform,
                                  uint32_t textureIdx, const glm::vec3 tint,
                                  const glm::vec2& uvScale);
};



// MARK: Render2D
struct Render2D
{

    static FQuadRender*  quadData;
    static FRender2dData data;


    Render2D()          = default;
    virtual ~Render2D() = default;


    static void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    static void destroy();

    static void onUpdate(float dt);
    static void onRender();


    static void begin(const FRender2dContext& ctx);
    static void onImGui();
    static void onRenderGUI() {}
    static void end()
    {
        quadData->end();
        data.curCmdBuf    = nullptr;
        data.windowWidth  = 0;
        data.windowHeight = 0;
    }

    // Convenience wrappers - delegate to FQuadRender
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

    // World-space sprite (billboard, 2D game characters, world particles, etc.)
    static void makeWorldSprite(const glm::mat4& worldTransform,
                                ya::Ptr<Texture> texture = nullptr,
                                const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                                const glm::vec2& uvScale = {1.0f, 1.0f})
    {
        quadData->drawWorldTexture(worldTransform, texture, tint, uvScale);
    }

    static void makeText(const std::string& text, const glm::vec3& position, const glm::vec4& color, Font* font)
    {
        quadData->drawText(text, position, color, font);
    }
};


}; // namespace ya