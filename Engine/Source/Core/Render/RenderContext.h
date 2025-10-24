#pragma once

#include "Core/Base.h"
#include "Render/Render.h"
#include "Render/Core/RenderPass.h"
#include "Render/Core/CommandBuffer.h"

namespace ya
{

// Forward declaration
class RenderTarget;

/**
 * @brief RenderContext - Manages the lifecycle of core rendering resources
 * 
 * Responsibilities:
 * - Create and destroy IRender instance
 * - Manage multiple RenderTargets
 * - Allocate and manage CommandBuffers
 * - Provide rendering utilities
 * 
 * Note: RenderTarget owns IRenderPass, not RenderContext
 */
class RenderContext
{
public:
    RenderContext() = default;
    ~RenderContext() = default;

    /**
     * @brief Initialize render context with given configuration
     */
    void init(const RenderCreateInfo& createInfo);

    /**
     * @brief Cleanup all rendering resources
     */
    void destroy();

    /**
     * @brief Create a RenderTarget for the main swapchain
     * @param renderPass The render pass to use for this target
     * @return Pointer to the created RenderTarget (managed by caller)
     */
    RenderTarget* createSwapchainRenderTarget(IRenderPass* renderPass);

    /**
     * @brief Create a custom RenderTarget with specified size and buffer count
     * @param renderPass The render pass to use for this target
     * @param width Width of the render target
     * @param height Height of the render target
     * @param bufferCount Number of frame buffers (for ping-pong rendering)
     * @return Pointer to the created RenderTarget (managed by caller)
     */
    RenderTarget* createRenderTarget(IRenderPass* renderPass, uint32_t width, uint32_t height, uint32_t bufferCount = 1);

    /**
     * @brief Register a RenderTarget with this context
     * Used when RenderTarget is created externally
     */
    void registerRenderTarget(RenderTarget* target);

    /**
     * @brief Unregister and optionally delete a RenderTarget
     */
    void destroyRenderTarget(RenderTarget* target);

    // Getters
    IRender* getRender() const { return _render; }
    
    std::vector<std::shared_ptr<ICommandBuffer>>& getCommandBuffers() { return _commandBuffers; }
    const std::vector<std::shared_ptr<ICommandBuffer>>& getCommandBuffers() const { return _commandBuffers; }

    const std::vector<RenderTarget*>& getRenderTargets() const { return _renderTargets; }

    uint32_t getSwapchainWidth() const { return _render ? _render->getSwapchainWidth() : 0; }
    uint32_t getSwapchainHeight() const { return _render ? _render->getSwapchainHeight() : 0; }
    uint32_t getSwapchainImageCount() const { return _render ? _render->getSwapchainImageCount() : 0; }

    void getWindowSize(int& width, int& height) const 
    { 
        if (_render) _render->getWindowSize(width, height); 
    }

    void setVsync(bool enabled) 
    { 
        if (_render) _render->setVsync(enabled); 
    }

private:
    IRender* _render = nullptr;
    std::vector<std::shared_ptr<ICommandBuffer>> _commandBuffers;
    std::vector<RenderTarget*> _renderTargets; // Track all render targets

    // Configuration
    RenderCreateInfo _createInfo;
};

} // namespace ya
