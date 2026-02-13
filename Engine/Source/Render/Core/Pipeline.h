#pragma once


#include "Render/RenderDefines.h"
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct IRender;
struct IRenderPass;
struct IDescriptorSetLayout;
struct ICommandBuffer;

// Forward declarations
struct PushConstantRange;
struct GraphicsPipelineCreateInfo;

/**
 * @brief Generic pipeline layout interface
 */
struct IPipelineLayout : public plat_base<IPipelineLayout>
{
  public:
    virtual ~IPipelineLayout() = default;

    IPipelineLayout()                                  = default;
    IPipelineLayout(const IPipelineLayout&)            = delete;
    IPipelineLayout& operator=(const IPipelineLayout&) = delete;
    IPipelineLayout(IPipelineLayout&&)                 = default;
    IPipelineLayout& operator=(IPipelineLayout&&)      = default;

    /**
     * @brief Get the native handle (backend-specific)
     */
    virtual void* getHandle() const = 0;

    /**
     * @brief Get typed native handle
     */
    template <typename T>
    T getHandleAs() const { return static_cast<T>(getHandle()); }

    /**
     * @brief Get the label/name of this pipeline layout
     */
    virtual const std::string& getLabel() const = 0;

    /**
     * @brief Factory method to create pipeline layout
     */
    static std::shared_ptr<IPipelineLayout> create(
        IRender*                                                  render,
        const std::string&                                        label,
        const std::vector<PushConstantRange>&                     pushConstants,
        const std::vector<std::shared_ptr<IDescriptorSetLayout>>& descriptorSetLayouts);
};

/**
 * @brief Generic graphics pipeline interface
 */
struct IGraphicsPipeline
{
  public:
    virtual ~IGraphicsPipeline() = default;

    IGraphicsPipeline()                                    = default;
    IGraphicsPipeline(const IGraphicsPipeline&)            = delete;
    IGraphicsPipeline& operator=(const IGraphicsPipeline&) = delete;
    IGraphicsPipeline(IGraphicsPipeline&&)                 = default;
    IGraphicsPipeline& operator=(IGraphicsPipeline&&)      = default;

    /**
     * @brief Recreate the pipeline with new configuration
     */
    virtual bool recreate(const GraphicsPipelineCreateInfo& ci) = 0;

    /**
     * @brief Bind this pipeline to a command buffer
     */
    [[deprecated("use ICommandBuffer::bindPipeline instead")]]
    virtual void bind(CommandBufferHandle commandBuffer) = 0;

    /**
     * @brief Get the native handle (backend-specific)
     */
    virtual void* getHandle() const = 0;

    /**
     * @brief Get typed native handle
     */
    template <typename T>
    T getHandleAs() const { return static_cast<T>(getHandle()); }

    /**
     * @brief Get the pipeline name
     */
    virtual const std::string& getName() const                                                  = 0;
    virtual void               reloadShaders(std::optional<GraphicsPipelineCreateInfo> ci = {}) = 0;
    virtual void               tryUpdateShader()                                                = 0;

    /**
     * @brief Factory method to create graphics pipeline
     */
    static std::shared_ptr<IGraphicsPipeline> create(IRender* render);
};

} // namespace ya
