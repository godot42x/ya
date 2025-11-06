#include "RenderContext.h"
#include "Core/Log.h"
#include "Render/Core/IRenderTarget.h"


namespace ya
{

void RenderContext::init(const RenderCreateInfo &createInfo)
{
    _createInfo = createInfo;

    // Create render instance
    _render = IRender::create(createInfo);
    YA_CORE_ASSERT(_render, "Failed to create IRender instance");

    // Initialize render backend
    _render->init(createInfo);

    // Set VSync
    setVsync(createInfo.swapchainCI.bVsync);

    // Allocate command buffers
    _render->allocateCommandBuffers(getSwapchainImageCount(), _commandBuffers);

    YA_CORE_INFO("RenderContext initialized successfully");
}

void RenderContext::destroy()
{
    if (!_render) {
        return;
    }
    _render->waitIdle();

    // Cleanup all render targets
    // for (auto *rt : _renderTargets) {
    //     delete rt;
    // }
    // _renderTargets.clear();
    _ownedRenderTargets.clear();
    _commandBuffers.clear();

    _render->destroy();
    delete _render;
    _render = nullptr;

    YA_CORE_INFO("RenderContext destroyed");
}

IRenderTarget *RenderContext::createSwapchainRenderTarget(IRenderPass *renderPass)
{
    YA_CORE_ASSERT(_render, "RenderContext not initialized");
    YA_CORE_ASSERT(renderPass, "RenderPass is null");

    auto rt = ya::createRenderTarget(renderPass);
    // _renderTargets.push_back(rt.get());
    _ownedRenderTargets.push_back(rt);

    YA_CORE_INFO("Created swapchain RenderTarget");
    return rt.get();
}

IRenderTarget *RenderContext::createRenderTarget(IRenderPass *renderPass, uint32_t width, uint32_t height, uint32_t bufferCount)
{
    YA_CORE_ASSERT(_render, "RenderContext not initialized");
    YA_CORE_ASSERT(renderPass, "RenderPass is null");

    auto rt = ya::createRenderTarget(renderPass, bufferCount, glm::vec2(width, height));
    // _renderTargets.push_back(rt.get());
    _ownedRenderTargets.push_back(rt);

    YA_CORE_INFO("Created custom RenderTarget: {}x{}, {} buffers", width, height, bufferCount);
    return rt.get();
}

void RenderContext::registerRenderTarget(IRenderTarget *target)
{
    // if (target && std::find(_renderTargets.begin(), _renderTargets.end(), target) == _renderTargets.end()) {
    //     _renderTargets.push_back(target);
    // }
    UNIMPLEMENTED();
}

void RenderContext::destroyRenderTarget(IRenderTarget *target)
{
    // auto it = std::find(_renderTargets.begin(), _renderTargets.end(), target);
    // if (it != _renderTargets.end()) {
    //     delete it._Ptr;
    //     _renderTargets.erase(it);
    //     YA_CORE_INFO("RenderTarget destroyed");
    // }
    UNIMPLEMENTED();
}

} // namespace ya
