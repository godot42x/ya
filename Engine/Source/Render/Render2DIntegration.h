#pragma once

#include "UIRender.h"
#include "RenderManager.h"
#include "Core/EditorCamera.h"

namespace Neon {

/**
 * @brief Integration class for 2D rendering with the main render system
 * This class bridges the F2DRender system with the main application and Vulkan renderer
 */
class Render2DIntegration {
private:
    static bool s_initialized;
    static EditorCamera* s_camera;
    static glm::mat4 s_projectionMatrix;
    
public:
    /**
     * @brief Initialize the 2D rendering system
     * Should be called after the main Vulkan renderer is initialized
     */
    static bool initialize();
    
    /**
     * @brief Shutdown the 2D rendering system
     */
    static void shutdown();
    
    /**
     * @brief Set the camera for 2D rendering
     * @param camera Pointer to the camera that provides view/projection matrices
     */
    static void setCamera(EditorCamera* camera);
    
    /**
     * @brief Set orthographic projection for 2D rendering
     * @param left Left plane of the projection
     * @param right Right plane of the projection  
     * @param bottom Bottom plane of the projection
     * @param top Top plane of the projection
     */
    static void setOrthographicProjection(float left, float right, float bottom, float top);
    
    /**
     * @brief Set screen size for automatic orthographic projection setup
     * @param width Screen width
     * @param height Screen height
     */
    static void setScreenSize(float width, float height);
    
    /**
     * @brief Begin a new 2D rendering frame
     * Call this at the start of each frame before any draw commands
     */
    static void beginFrame();
    
    /**
     * @brief End the current 2D rendering frame
     * Call this at the end of each frame to finalize rendering
     */
    static void endFrame();
    
    /**
     * @brief Register with the render manager for automatic rendering
     * @param renderManager The main render manager
     * @param stage The render stage to execute 2D rendering in (typically UI2D)
     * @param priority Priority within the stage (higher = earlier)
     */
    static void registerWithRenderManager(RenderPassManager* renderManager, 
                                         RenderStage stage = RenderStage::UI2D, 
                                         int priority = 0);
    
    /**
     * @brief Render function to be called by render manager
     * @param cmdList Command list for recording render commands
     */
    static void renderCallback(RHICmdList* cmdList);
    
    /**
     * @brief Get rendering statistics
     */
    static void getStats(uint32_t& drawCalls, uint32_t& vertexCount, uint32_t& quadCount);
    
    /**
     * @brief Example usage - render a simple UI
     * This demonstrates how to use the 2D renderer
     */
    static void renderExampleUI();
};

} // namespace Neon
