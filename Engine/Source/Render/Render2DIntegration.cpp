#include "Render2DIntegration.h"
#include "Core/App/App.h"
#include "Core/Log.h"


namespace Neon
{

bool          Render2DIntegration::s_initialized      = false;
EditorCamera *Render2DIntegration::s_camera           = nullptr;
glm::mat4     Render2DIntegration::s_projectionMatrix = glm::mat4(1.0f);

bool Render2DIntegration::initialize()
{
    if (s_initialized) {
        NE_CORE_WARN("Render2DIntegration already initialized");
        return true;
    }

    // Initialize the F2DRender system
    if (!F2DRender::initialize()) {
        NE_CORE_ERROR("Failed to initialize F2DRender");
        return false;
    }

    // Set up default orthographic projection (800x600)
    setScreenSize(800.0f, 600.0f);

    s_initialized = true;
    NE_CORE_INFO("Render2DIntegration initialized successfully");
    return true;
}

void Render2DIntegration::shutdown()
{
    if (!s_initialized)
        return;

    F2DRender::shutdown();
    s_initialized = false;
    s_camera      = nullptr;

    NE_CORE_INFO("Render2DIntegration shutdown complete");
}

void Render2DIntegration::setCamera(EditorCamera *camera)
{
    s_camera = camera;
    if (camera) {
        // Use camera's projection matrix
        s_projectionMatrix = camera->getProjectionMatrix();
    }
}

void Render2DIntegration::setOrthographicProjection(float left, float right, float bottom, float top)
{
    s_projectionMatrix = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);
}

void Render2DIntegration::setScreenSize(float width, float height)
{
    // Create orthographic projection that maps screen coordinates to NDC
    // Origin at top-left, Y increases downward (typical UI coordinate system)
    setOrthographicProjection(0.0f, width, height, 0.0f);
}

void Render2DIntegration::beginFrame()
{
    if (!s_initialized) {
        NE_CORE_ERROR("Render2DIntegration not initialized");
        return;
    }

    // Update projection matrix from camera if available
    if (s_camera) {
        s_projectionMatrix = s_camera->getProjectionMatrix();
    }
    else {
        s_projectionMatrix = glm::mat4(1.0f); // Default identity if no camera
    }

    F2DRender::beginFrame(s_projectionMatrix);
}

void Render2DIntegration::endFrame()
{
    if (!s_initialized)
        return;

    F2DRender::endFrame();
}

void Render2DIntegration::registerWithRenderManager(RenderPassManager *renderManager,
                                                    RenderStage stage, int priority)
{
    if (!renderManager) {
        NE_CORE_ERROR("RenderPassManager is null");
        return;
    }

    renderManager->addPass(stage, renderCallback, priority);
    NE_CORE_INFO("Render2DIntegration registered with RenderPassManager");
}

void Render2DIntegration::renderCallback(RHICmdList *cmdList)
{
    if (!s_initialized)
        return;

    // Execute the 2D rendering
    F2DRender::render();
}

void Render2DIntegration::getStats(uint32_t &drawCalls, uint32_t &vertexCount, uint32_t &quadCount)
{
    // TODO: Implement stats retrieval from F2DRender
    drawCalls   = 0;
    vertexCount = 0;
    quadCount   = 0;
}

void Render2DIntegration::renderExampleUI()
{
    if (!s_initialized)
        return;

    // Example: Draw some colored rectangles
    F2DRender::drawQuad({100.0f, 100.0f}, {200.0f, 100.0f}, {1.0f, 0.0f, 0.0f, 1.0f}); // Red
    F2DRender::drawQuad({150.0f, 150.0f}, {100.0f, 100.0f}, {0.0f, 1.0f, 0.0f, 1.0f}); // Green
    F2DRender::drawQuad({200.0f, 200.0f}, {150.0f, 75.0f}, {0.0f, 0.0f, 1.0f, 1.0f});  // Blue

    // Example: Draw rotating rectangle
    static float rotation = 0.0f;
    rotation += 1.0f;                                                                          // Rotate 1 degree per frame
    F2DRender::drawQuad({400.0f, 300.0f}, {80.0f, 80.0f}, {1.0f, 1.0f, 0.0f, 1.0f}, rotation); // Yellow, rotating

    // Example: Draw UI button-like rectangles
    F2DRender::drawQuad({50.0f, 500.0f}, {120.0f, 40.0f}, {0.3f, 0.3f, 0.3f, 1.0f});  // Button background
    F2DRender::drawQuad({200.0f, 500.0f}, {120.0f, 40.0f}, {0.5f, 0.2f, 0.8f, 1.0f}); // Another button
}

} // namespace Neon
