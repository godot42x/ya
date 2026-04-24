#pragma once

#include "Render/Pipelines/DebugPrimitives.h"

namespace ya
{

struct RenderRuntime;

struct DebugRenderSystem
{
    static DebugRenderSystem& get();

    void init(IRender* render);
    void destroy();

    // Game thread: enqueue for next render frame.
    void addLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color = glm::vec4(1.0f));
    void addSphere(const glm::vec3& center, float radius, const glm::vec4& color = glm::vec4(1.0f));
    void addCylinder(const glm::mat4& model, const glm::vec4& color = glm::vec4(1.0f));
    void addCone(const glm::mat4& model, const glm::vec4& color = glm::vec4(1.0f));

    // Render thread: visible in current frame.
    void addLineImmediate(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color = glm::vec4(1.0f));
    void addSphereImmediate(const glm::vec3& center, float radius, const glm::vec4& color = glm::vec4(1.0f));
    void addCylinderImmediate(const glm::mat4& model, const glm::vec4& color = glm::vec4(1.0f));
    void addConeImmediate(const glm::mat4& model, const glm::vec4& color = glm::vec4(1.0f));

    void beginFrame();
    void refreshPipelineFormats(const IRenderTarget* viewportRT);
    void draw(ICommandBuffer* cmdBuf,
              uint32_t viewportWidth,
              uint32_t viewportHeight,
              const glm::mat4& projection,
              const glm::mat4& view);
    void renderGUI();

    DebugPrimitives& primitives() { return _primitives; }
    const DebugPrimitives& primitives() const { return _primitives; }

  private:
    DebugPrimitives _primitives;
};

} // namespace ya
