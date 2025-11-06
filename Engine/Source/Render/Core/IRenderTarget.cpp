#include "IRenderTarget.h"
#include "Core/App/App.h"


// Platform-specific includes
#if USE_VULKAN
    #include "Platform/Render/Vulkan/VulkanRenderTarget.h"
#endif

namespace ya
{

// Factory functions - create platform-specific implementations
std::shared_ptr<IRenderTarget> createRenderTarget(IRenderPass *renderPass)
{
#if USE_VULKAN
    return makeShared<VulkanRenderTarget>(renderPass);
#else
    #error "Platform not supported"
#endif
}

std::shared_ptr<IRenderTarget> createRenderTarget(IRenderPass *renderPass, uint32_t frameBufferCount, glm::vec2 extent)
{
#if USE_VULKAN
    return makeShared<VulkanRenderTarget>(renderPass, frameBufferCount, extent);
#else
    #error "Platform not supported"
#endif
}

} // namespace ya
