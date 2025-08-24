
#pragma once



#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"
#include "Platform/Render/Vulkan/VulkanRenderPass.h"
namespace ya
{

struct RenderTarget
{

    VulkanRenderPass *_renderPass       = nullptr;
    int               subpassRef        = -1; // TODO: a RT should related to a subpass
    uint32_t          _frameBufferCount = 0;
    VkExtent2D        _extent           = {0, 0};

    std::vector<std::shared_ptr<VulkanFrameBuffer>> _frameBuffers;
    std::vector<VkClearValue>                       _clearValues;

    uint32_t _currentFrameIndex = 0; // Current frame index for this render target


    bool bSwapChainTarget = false; // Whether this render target is the swap chain target
    bool bBeginTarget     = false; // Whether this render target is the begin target

    bool bDirty = false;

  public:

    // TODO : abstract API-independent class -> "IRenderPass"
    RenderTarget(VulkanRenderPass *renderPass);
    RenderTarget(VulkanRenderPass *renderPass, uint32_t frameBufferCount, glm::vec2 extent);
    virtual ~RenderTarget() = default;

    void init();
    void recreate();
    void destroy();

    void begin(void *cmdBuf);
    void end(void *cmdBuf);

  public:
    void setExtent(VkExtent2D extent);
    void setBufferCount(uint32_t count);
    void setColorClearValue(VkClearValue clearValue);
    void setColorClearValue(uint32_t attachmentIdx, VkClearValue clearValue);

    void setDepthStencilClearValue(VkClearValue clearValue);
    void setDepthStencilClearValue(uint32_t attachmentIdx, VkClearValue clearValue);

    [[nodiscard]] VulkanRenderPass  *getRenderPass() const { return _renderPass; }
    [[nodiscard]] VulkanFrameBuffer *getFrameBuffer() const { return _frameBuffers[_currentFrameIndex].get(); }
};

}; // namespace ya