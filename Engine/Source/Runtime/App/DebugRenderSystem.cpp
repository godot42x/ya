#include "DebugRenderSystem.h"

namespace ya
{

DebugRenderSystem& DebugRenderSystem::get()
{
    static DebugRenderSystem instance;
    return instance;
}

void DebugRenderSystem::init(IRender* render)
{
    if (_primitives._render == render) {
        return;
    }

    if (_primitives._render) {
        _primitives.destroy();
    }
    _primitives.init(render);
}

void DebugRenderSystem::destroy()
{
    _primitives.destroy();
}

void DebugRenderSystem::addLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    _primitives.addLine(from, to, color);
}

void DebugRenderSystem::addSphere(const glm::vec3& center, float radius, const glm::vec4& color)
{
    _primitives.addSphere(center, radius, color);
}

void DebugRenderSystem::addCylinder(const glm::mat4& model, const glm::vec4& color)
{
    _primitives.addCylinder(model, color);
}

void DebugRenderSystem::addCone(const glm::mat4& model, const glm::vec4& color)
{
    _primitives.addCone(model, color);
}

void DebugRenderSystem::addLineImmediate(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    _primitives.addLineImmediate(from, to, color);
}

void DebugRenderSystem::addSphereImmediate(const glm::vec3& center, float radius, const glm::vec4& color)
{
    _primitives.addSphereImmediate(center, radius, color);
}

void DebugRenderSystem::addCylinderImmediate(const glm::mat4& model, const glm::vec4& color)
{
    _primitives.addCylinderImmediate(model, color);
}

void DebugRenderSystem::addConeImmediate(const glm::mat4& model, const glm::vec4& color)
{
    _primitives.addConeImmediate(model, color);
}

void DebugRenderSystem::beginFrame()
{
    _primitives.beginFrame();
}

void DebugRenderSystem::refreshPipelineFormats(const IRenderTarget* viewportRT)
{
    _primitives.refreshPipelineFormats(viewportRT);
}

void DebugRenderSystem::draw(ICommandBuffer*  cmdBuf,
                             uint32_t         viewportWidth,
                             uint32_t         viewportHeight,
                             const glm::mat4& projection,
                             const glm::mat4& view)
{
    _primitives.draw(cmdBuf, viewportWidth, viewportHeight, projection, view);
}

void DebugRenderSystem::renderGUI()
{
    _primitives.renderGUI();
}

} // namespace ya
