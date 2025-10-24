#pragma once

#include "Handle.h"
#include "Render/Core/Sampler.h"
#include "Render/RenderDefines.h"
#include <cstdint>
#include <memory>
#include <vector>



namespace ya
{

// Forward declarations
struct IRender;

// Tag types for different handle kinds
struct BufferHandleTag
{};
struct ImageViewHandleTag
{};
struct DescriptorSetHandleTag
{};

using BufferHandle        = Handle<BufferHandleTag>;
using ImageViewHandle     = Handle<ImageViewHandleTag>;
using DescriptorSetHandle = Handle<DescriptorSetHandleTag>;

// Buffer info for descriptor updates
struct DescriptorBufferInfo
{
    BufferHandle buffer; // Backend-specific buffer handle (VkBuffer, etc.)
    uint64_t     offset = 0;
    uint64_t     range  = 0;

    DescriptorBufferInfo() = default;
    DescriptorBufferInfo(BufferHandle buf, uint64_t off, uint64_t rng)
        : buffer(buf), offset(off), range(rng) {}
};

// Image info for descriptor updates
struct DescriptorImageInfo
{
    SamplerHandle   sampler;   // Backend-specific sampler handle
    ImageViewHandle imageView; // Backend-specific image view handle
    EImageLayout::T imageLayout = EImageLayout::Undefined;

    DescriptorImageInfo() = default;
    DescriptorImageInfo(SamplerHandle samp, ImageViewHandle view, EImageLayout::T layout)
        : sampler(samp), imageView(view), imageLayout(layout) {}
};

// Descriptor write operation
struct WriteDescriptorSet
{
    DescriptorSetHandle        dstSet; // Backend-specific descriptor set handle
    uint32_t                   dstBinding      = 0;
    uint32_t                   dstArrayElement = 0;
    EPipelineDescriptorType::T descriptorType  = EPipelineDescriptorType::UniformBuffer;
    uint32_t                   descriptorCount = 1;

    // Only one of these should be non-null based on descriptorType
    const DescriptorBufferInfo *pBufferInfo      = nullptr;
    const DescriptorImageInfo  *pImageInfo       = nullptr;
    const void                 *pTexelBufferView = nullptr; // For texel buffer views

    WriteDescriptorSet() = default;
};

// Descriptor copy operation (if needed in the future)
struct CopyDescriptorSet
{
    DescriptorSetHandle srcSet;
    uint32_t            srcBinding      = 0;
    uint32_t            srcArrayElement = 0;
    DescriptorSetHandle dstSet;
    uint32_t            dstBinding      = 0;
    uint32_t            dstArrayElement = 0;
    uint32_t            descriptorCount = 1;
};

/**
 * @brief Abstract interface for descriptor set layout
 * Describes the bindings in a descriptor set
 */
class IDescriptorSetLayout
{
  public:
    virtual ~IDescriptorSetLayout() = default;

    IDescriptorSetLayout()                                        = default;
    IDescriptorSetLayout(const IDescriptorSetLayout &)            = delete;
    IDescriptorSetLayout &operator=(const IDescriptorSetLayout &) = delete;
    IDescriptorSetLayout(IDescriptorSetLayout &&)                 = default;
    IDescriptorSetLayout &operator=(IDescriptorSetLayout &&)      = default;

    /**
     * @brief Create a descriptor set layout
     * @param render The render backend
     * @param layout The descriptor set layout description
     * @return Shared pointer to the created descriptor set layout
     */
    static std::shared_ptr<IDescriptorSetLayout> create(IRender *render, const DescriptorSetLayout &layout);

    /**
     * @brief Get the layout information
     */
    virtual const DescriptorSetLayout &getLayoutInfo() const = 0;

    /**
     * @brief Get backend-specific handle (implementation should cast to appropriate type)
     */
    virtual void *getHandle() const = 0;

    /**
     * @brief Get typed backend-specific handle
     */
    template <typename T>
    T getHandleAs() const
    {
        // Note: For getHandleAs, T is the handle type (like VkDescriptorSetLayout), not a class
        // So we don't use is_base_of check here
        return static_cast<T>(getHandle());
    }
};

// Forward declaration
struct IRender;

/**
 * @brief Abstract interface for descriptor pool
 * Manages allocation of descriptor sets
 */
class IDescriptorPool
{
  public:
    virtual ~IDescriptorPool() = default;

    IDescriptorPool()                                   = default;
    IDescriptorPool(const IDescriptorPool &)            = delete;
    IDescriptorPool &operator=(const IDescriptorPool &) = delete;
    IDescriptorPool(IDescriptorPool &&)                 = default;
    IDescriptorPool &operator=(IDescriptorPool &&)      = default;

    /**
     * @brief Factory method to create descriptor pool for the current render backend
     * @param render The render interface
     * @param ci Pool creation info
     * @return Shared pointer to the created descriptor pool
     */
    static std::shared_ptr<IDescriptorPool> create(IRender *render, const DescriptorPoolCreateInfo &ci);

    /**
     * @brief Allocate N descriptor sets of the same layout
     * @param layout The descriptor set layout
     * @param count Number of sets to allocate
     * @param outSets Output vector of allocated descriptor set handles
     * @return true if allocation succeeded
     */
    virtual bool allocateDescriptorSets(
        const std::shared_ptr<IDescriptorSetLayout> &layout,
        uint32_t                                     count,
        std::vector<DescriptorSetHandle>            &outSets) = 0;

    /**
     * @brief Reset the descriptor pool
     */
    virtual void reset() = 0;

    /**
     * @brief Set debug name for the pool
     */
    virtual void setDebugName(const char *name) = 0;

    /**
     * @brief Get backend-specific handle
     */
    virtual void *getHandle() const = 0;
};

/**
 * @brief Abstract interface for descriptor set operations
 * Provides utilities for updating descriptor sets
 */
class IDescriptorSetHelper
{
  public:
    virtual ~IDescriptorSetHelper() = default;

    IDescriptorSetHelper()                                        = default;
    IDescriptorSetHelper(const IDescriptorSetHelper &)            = delete;
    IDescriptorSetHelper &operator=(const IDescriptorSetHelper &) = delete;
    IDescriptorSetHelper(IDescriptorSetHelper &&)                 = default;
    IDescriptorSetHelper &operator=(IDescriptorSetHelper &&)      = default;

    /**
     * @brief Update descriptor sets with writes and copies
     * @param writes Vector of write operations
     * @param copies Vector of copy operations
     */
    virtual void updateDescriptorSets(
        const std::vector<WriteDescriptorSet> &writes,
        const std::vector<CopyDescriptorSet>  &copies = {}) = 0;

    /**
     * @brief Helper to generate a buffer write descriptor
     */
    static WriteDescriptorSet genBufferWrite(
        void                       *dstSet,
        uint32_t                    dstBinding,
        uint32_t                    dstArrayElement,
        EPipelineDescriptorType::T  descriptorType,
        const DescriptorBufferInfo *pBufferInfo,
        uint32_t                    descriptorCount = 1)
    {
        WriteDescriptorSet write;
        write.dstSet           = dstSet;
        write.dstBinding       = dstBinding;
        write.dstArrayElement  = dstArrayElement;
        write.descriptorType   = descriptorType;
        write.descriptorCount  = descriptorCount;
        write.pBufferInfo      = pBufferInfo;
        write.pImageInfo       = nullptr;
        write.pTexelBufferView = nullptr;
        return write;
    }

    /**
     * @brief Helper to generate an image write descriptor
     */
    static WriteDescriptorSet genImageWrite(
        void                      *dstSet,
        uint32_t                   dstBinding,
        uint32_t                   dstArrayElement,
        EPipelineDescriptorType::T descriptorType,
        const DescriptorImageInfo *pImageInfo,
        uint32_t                   descriptorCount = 1)
    {
        WriteDescriptorSet write;
        write.dstSet           = dstSet;
        write.dstBinding       = dstBinding;
        write.dstArrayElement  = dstArrayElement;
        write.descriptorType   = descriptorType;
        write.descriptorCount  = descriptorCount;
        write.pBufferInfo      = nullptr;
        write.pImageInfo       = pImageInfo;
        write.pTexelBufferView = nullptr;
        return write;
    }

    /**
     * @brief Helper to generate a texel buffer write descriptor
     */
    static WriteDescriptorSet genTexelBufferWrite(
        void                      *dstSet,
        uint32_t                   dstBinding,
        uint32_t                   dstArrayElement,
        EPipelineDescriptorType::T descriptorType,
        const void                *pTexelBufferView,
        uint32_t                   descriptorCount = 1)
    {
        WriteDescriptorSet write;
        write.dstSet           = dstSet;
        write.dstBinding       = dstBinding;
        write.dstArrayElement  = dstArrayElement;
        write.descriptorType   = descriptorType;
        write.descriptorCount  = descriptorCount;
        write.pBufferInfo      = nullptr;
        write.pImageInfo       = nullptr;
        write.pTexelBufferView = pTexelBufferView;
        return write;
    }
};

} // namespace ya
