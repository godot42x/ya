
#pragma once

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Texture.h"
#include "glm/glm.hpp"

#include "Core/App/App.h"
#include "Core/Base.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"

namespace ya
{



struct FRender2dData
{
    uint32_t     windowWidth  = 800;
    uint32_t     windowHeight = 600;
    ECullMode::T cullMode     = ECullMode::Back;
};



struct Render2D
{

    Render2D()          = default;
    virtual ~Render2D() = default;


    static void init(IRender *render, IRenderPass *renderpass);
    static void destroy();

    static void onUpdate();
    static void begin(ICommandBuffer *cmdBuf);
    static void onImGui();
    static void end();

    static void makeSprite(const glm::vec3         &position,
                           const glm::vec2         &size,
                           std::shared_ptr<Texture> texture = nullptr,
                           const glm::vec4         &tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                           const glm::vec2         &uvScale = {1.0f, 1.0f});

    // void makeRotatedSprite(const glm::vec2 &position, const glm::vec2 &size, const glm::vec4 &color, float rotation) {}
    // void drawText(const std::string &text, const glm::vec2 &position, const glm::vec4 &color) {}
};


struct FQuadData
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

    struct FrameUBO
    {
        glm::mat4 matViewProj = glm::mat4(1.0f);
    };

    IRender *_render = nullptr;

    std::shared_ptr<IBuffer> _vertexBuffer;
    std::shared_ptr<IBuffer> _indexBuffer;

    FQuadData::Vertex *vertexPtr     = nullptr;
    FQuadData::Vertex *vertexPtrHead = nullptr;

    uint32_t vertexCount = 0;
    uint32_t indexCount  = 0;


    PipelineDesc                       _pipelineDesc;
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

    void init(IRender *render, IRenderPass *renderPass);
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
};
}; // namespace ya