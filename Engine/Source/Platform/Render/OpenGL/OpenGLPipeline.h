#pragma once

#include "Core/Base.h"
#include "Core/FName.h"
#include "Render/Core/Pipeline.h"
#include "Render/RenderDefines.h"
#include "Render/Shader.h"
#include "glad/glad.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

struct OpenGLRender;
struct OpenGLRenderPass;
struct ICommandBuffer;

/**
 * @brief OpenGL pipeline layout
 */
struct OpenGLPipelineLayout : public IPipelineLayout
{
  private:
    std::string   _label;
    OpenGLRender *_render = nullptr;

    // Store push constant ranges (implemented as uniforms in OpenGL)
    std::vector<PushConstantRange> _pushConstants;

  public:
    OpenGLPipelineLayout(OpenGLRender *render, std::string label = std::string("None"))
        : _label(std::move(label)), _render(render) {}

    ~OpenGLPipelineLayout() override = default;

    OpenGLPipelineLayout(const OpenGLPipelineLayout &)            = delete;
    OpenGLPipelineLayout &operator=(const OpenGLPipelineLayout &) = delete;
    OpenGLPipelineLayout(OpenGLPipelineLayout &&)                 = default;
    OpenGLPipelineLayout &operator=(OpenGLPipelineLayout &&)      = default;

    void create(const std::vector<PushConstantRange>                     &pushConstants,
                const std::vector<std::shared_ptr<IDescriptorSetLayout>> &layouts);

    // IPipelineLayout interface
    void              *getHandle() const override { return (void *)this; }
    const std::string &getLabel() const override { return _label; }

    const std::vector<PushConstantRange> &getPushConstants() const { return _pushConstants; }
};

/**
 * @brief OpenGL graphics pipeline
 */
struct OpenGLPipeline : public ya::IGraphicsPipeline
{
  private:
    FName         _name;
    GLuint        _program         = 0;
    OpenGLRender *_render          = nullptr;
    OpenGLRenderPass *_renderPass  = nullptr;
    OpenGLPipelineLayout *_pipelineLayout = nullptr;

    ya::GraphicsPipelineCreateInfo _ci;

    // Cached shader locations
    std::unordered_map<std::string, GLint> _uniformLocations;

    // Pipeline state
    struct State
    {
        ECullMode::T cullMode         = ECullMode::Back;
        bool         depthTestEnabled = true;
        ECompareOp::T depthCompareOp  = ECompareOp::Less;
        bool         depthWriteEnabled = true;
        bool         blendEnabled     = false;
        // Add more state as needed
    } _state;

  public:
    OpenGLPipeline(OpenGLRender *render, OpenGLRenderPass *renderPass, OpenGLPipelineLayout *pipelineLayout)
        : _render(render), _renderPass(renderPass), _pipelineLayout(pipelineLayout) {}

    ~OpenGLPipeline() override;

    OpenGLPipeline(const OpenGLPipeline &)            = delete;
    OpenGLPipeline &operator=(const OpenGLPipeline &) = delete;
    OpenGLPipeline(OpenGLPipeline &&)                 = default;
    OpenGLPipeline &operator=(OpenGLPipeline &&)      = default;

    // IGraphicsPipeline interface
    bool recreate(const GraphicsPipelineCreateInfo &ci) override;
    void bind(CommandBufferHandle commandBuffer) override;
    void              *getHandle() const override { return (void *)(uintptr_t)_program; }
    const std::string &getName() const override
    {
        static std::string name_cache;
        name_cache = std::string(_name._data);
        return name_cache;
    }

    // OpenGL-specific methods
    GLuint getProgram() const { return _program; }
    GLint  getUniformLocation(const std::string &name);

    void cleanup();

  private:
    GLuint createShaderModule(GLenum type, const std::vector<uint32_t> &spv_binary);
    GLuint compileShader(GLenum type, const std::string &source);
    GLuint linkProgram(const std::vector<GLuint> &shaders);
    void   applyPipelineState();
};

} // namespace ya
