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
struct ComputePipelineCreateInfo;

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

struct IPipeline
{
    virtual ~IPipeline() = default;
};

/**
 * @brief Generic compute pipeline interface
 */
struct IComputePipeline : public IPipeline
{
  public:
    IComputePipeline()                                   = default;
    IComputePipeline(const IComputePipeline&)            = delete;
    IComputePipeline& operator=(const IComputePipeline&) = delete;
    IComputePipeline(IComputePipeline&&)                 = default;
    IComputePipeline& operator=(IComputePipeline&&)      = default;

    static std::shared_ptr<IComputePipeline> create(IRender* render);

    virtual bool               recreate(const ComputePipelineCreateInfo& ci) = 0;
    virtual void*              getHandle() const                             = 0;
    virtual const std::string& getName() const                               = 0;

    template <typename T>
    T getHandleAs() const { return static_cast<T>(getHandle()); }
};

/**
 * @brief Generic graphics pipeline interface
 */
struct IGraphicsPipeline : public IPipeline
{
  public:
    virtual ~IGraphicsPipeline() = default;

    IGraphicsPipeline()                                    = default;
    IGraphicsPipeline(const IGraphicsPipeline&)            = delete;
    IGraphicsPipeline& operator=(const IGraphicsPipeline&) = delete;
    IGraphicsPipeline(IGraphicsPipeline&&)                 = default;
    IGraphicsPipeline& operator=(IGraphicsPipeline&&)      = default;
    /**
     * @brief Factory method to create graphics pipeline
     */
    static std::shared_ptr<IGraphicsPipeline> create(IRender* render);

    virtual bool recreate(const GraphicsPipelineCreateInfo& ci) = 0;
    virtual void beginFrame()                                   = 0;
    virtual void renderGUI()                                    = 0;

    /**
     * @brief Bind this pipeline to a command buffer
     */
    [[deprecated("use ICommandBuffer::bindPipeline instead")]]
    virtual void bind(CommandBufferHandle commandBuffer) = 0;


    virtual void* getHandle() const = 0;
    template <typename T>
    T getHandleAs() const { return static_cast<T>(getHandle()); }

    /**
     * @brief Get the pipeline name
     */
    virtual const std::string& getName() const = 0;
    virtual const GraphicsPipelineCreateInfo& getDesc() const = 0;

    virtual void            updateDesc(GraphicsPipelineCreateInfo ci)                          = 0;
    virtual void            setSampleCount(ESampleCount::T sampleCount)                        = 0;
    virtual ESampleCount::T getSampleCount() const                                             = 0;
    virtual void            setCullMode(ECullMode::T cullMode)                                 = 0;
    virtual ECullMode::T    getCullMode() const                                                = 0;
    virtual void            setPolygonMode(EPolygonMode::T polygonMode)                        = 0;
    virtual EPolygonMode::T getPolygonMode() const                                             = 0;
    virtual void            setDepthBiasEnable(bool enable)                                    = 0;
    virtual void            setDepthBias(float constantFactor, float clamp, float slopeFactor) = 0;
    virtual void            setDepthCompareOp(ECompareOp::T op)                                = 0;
    virtual ECompareOp::T   getDepthCompareOp() const                                          = 0;


    bool isDirty() const { return _bDirty; }
    void markDirty() { _bDirty = true; }
    void clearDirty() { _bDirty = false; }

  protected:

    bool _bDirty = false;
};

} // namespace ya
