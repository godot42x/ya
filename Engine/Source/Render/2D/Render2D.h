
#pragma once

#include "glm/glm.hpp"

#include "Core/App/App.h"
#include "Core/Base.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Texture.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"
#include "Resource/TextureLibrary.h"

#include "Core/System/System.h"


#include "Resource/FontManager.h"

namespace ya
{



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
        glm::mat4 matViewProj = glm::mat4(1.0f);
    };

    IRender *_render = nullptr;

    std::shared_ptr<IBuffer> _vertexBuffer;
    std::shared_ptr<IBuffer> _indexBuffer;

    FQuadRender::Vertex *vertexPtr     = nullptr;
    FQuadRender::Vertex *vertexPtrHead = nullptr;

    uint32_t vertexCount = 0;
    uint32_t indexCount  = 0;


    PipelineLayoutDesc                 _pipelineDesc;
    std::shared_ptr<IPipelineLayout>   _pipelineLayout = nullptr;
    std::shared_ptr<IGraphicsPipeline> _pipeline       = nullptr;

    // descriptor set layout & pool
    std::shared_ptr<IDescriptorPool> _descriptorPool = nullptr;

    std::shared_ptr<IDescriptorSetLayout> _frameUboDSL    = nullptr;
    DescriptorSetHandle                   _frameUboDS     = {};
    std::shared_ptr<IBuffer>              _frameUBOBuffer = nullptr;

    std::shared_ptr<IDescriptorSetLayout>     _resourceDSL = nullptr;
    DescriptorSetHandle                       _resourceDS  = {};
    std::vector<TextureView>                  _textureViews;
    std::unordered_map<std::string, uint32_t> _textureLabel2Idx;
    static constexpr size_t                   TEXTURE_SET_SIZE     = 32;
    int                                       _lastPushTextureSlot = -1;

    // Note: White texture and default sampler are now provided by TextureLibrary

    void init(IRender *render);
    void destroy();

    void onImGui()
    {
        // control viewports &scissors
    }

    void begin();
    void end();

    bool shouldFlush() { return vertexCount >= MaxVertexCount - 4 || _lastPushTextureSlot + 1 >= (int)TEXTURE_SET_SIZE; }
    void flush(ICommandBuffer *cmdBuf);

    void updateFrameUBO(glm::mat4 viewProj);
    void updateResources();

  public:
    void drawTexture(const glm::vec3 &position,
                     const glm::vec2 &size,
                     ya::Ptr<Texture> texture = nullptr,
                     const glm::vec4 &tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                     const glm::vec2 &uvScale = {1.0f, 1.0f});

    void drawTexture(const glm::mat4 &transform,
                     ya::Ptr<Texture> texture = nullptr,
                     const glm::vec4 &tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                     const glm::vec2 &uvScale = {1.0f, 1.0f});

    void drawSubTexture(const glm::vec3 &position,
                        const glm::vec2 &size,
                        ya::Ptr<Texture> texture = nullptr,
                        const glm::vec4 &tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                        const glm::vec4 &uvRect  = glm::vec4(0.0f) // offset: xy , scale: zw
    );

    void drawText(const std::string &text, const glm::vec3 &position, const glm::vec4 &color, Font *font);

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
                _textureViews.push_back(TextureView{
                    .texture = texture,
                    .sampler = TextureLibrary::get().getDefaultSampler(),
                });
                auto idx                               = static_cast<uint32_t>(_textureViews.size() - 1);
                _textureLabel2Idx[texture->getLabel()] = idx;
                textureIdx                             = idx;
                _lastPushTextureSlot                   = static_cast<int>(idx);
            }
        }
        return textureIdx;
    }
};


struct FRender2dData
{
    // std::vector<stdptr<RenderSystem>> _systems;

    uint32_t        windowWidth  = 800;
    uint32_t        windowHeight = 600;
    ECullMode::T    cullMode     = ECullMode::Back;
    ICommandBuffer *curCmdBuf    = nullptr;



    int TextLayoutMode = 0;
    int viewMatrixMode = 0;
};

// MARK: Render2D
struct Render2D
{

    static FQuadRender  *quadData;
    static FRender2dData data;


    Render2D()          = default;
    virtual ~Render2D() = default;


    static void init(IRender *render);
    static void destroy();

    static void onUpdate(float dt);
    static void onRender();
    static void begin(ICommandBuffer *cmdBuf)
    {
        data.curCmdBuf = cmdBuf;
        quadData->begin();
    }
    static void onImGui();
    static void onRenderGUI() {}
    static void end()
    {
        quadData->end();
        data.curCmdBuf = nullptr;
    }

    // Convenience wrappers - delegate to FQuadRender
    static void makeSprite(const glm::vec3 &position,
                           const glm::vec2 &size,
                           ya::Ptr<Texture> texture = nullptr,
                           const glm::vec4 &tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                           const glm::vec2 &uvScale = {1.0f, 1.0f})
    {
        quadData->drawTexture(position, size, texture, tint, uvScale);
    }

    static void makeSprite(const glm::mat4 &transform,
                           ya::Ptr<Texture> texture = nullptr,
                           const glm::vec4 &tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                           const glm::vec2 &uvScale = {1.0f, 1.0f})
    {
        quadData->drawTexture(transform, texture, tint, uvScale);
    }

    static void makeText(const std::string &text, const glm::vec3 &position, const glm::vec4 &color, Font *font)
    {
        quadData->drawText(text, position, color, font);
    }
};


}; // namespace ya