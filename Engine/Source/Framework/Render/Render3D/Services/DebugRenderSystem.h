#pragma once

#include "Core/Api.h"
#include "RHI/Core/RenderAttachmentFormats.h"
#include "Render3D/Pipelines/DebugPrimitives.h"

namespace ya
{

struct RenderRuntime;

struct YA_RENDER_3D_API DebugRenderSystem
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
    void refreshPipelineFormats(const RenderAttachmentFormats& formats);
    void draw(ICommandBuffer* cmdBuf,
              uint32_t viewportWidth,
              uint32_t viewportHeight,
              const glm::mat4& projection,
              const glm::mat4& view);
    [[nodiscard]] DebugPrimitives::SettingsSnapshot buildSettingsSnapshot() const;
    [[nodiscard]] DebugPrimitives&       getPrimitives() { return _primitives; }
    [[nodiscard]] const DebugPrimitives& getPrimitives() const { return _primitives; }
    void requestSettings(const DebugPrimitives::SettingsSnapshot& settings);
    void setReverseViewportY(bool enabled);

  private:
    DebugPrimitives _primitives;
};

} // namespace ya
