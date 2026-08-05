#pragma once

#include "Buffer.h"
#include "Image.h"
#include "Sampler.h"

#include <memory>
#include <string>

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════
// Resource spec / replacement contract (FG-801)
//
// * All public creation specs (BufferCreateInfo, ImageCreateInfo,
//   ImageViewCreateInfo, SamplerDesc, ImportedImageDesc) are immutable and
//   backend-agnostic: no Vulkan/GL types leak into them.
// * Spec comparison is the single source for "does this resource need
//   replacement": isSameBufferCreateInfo / isSameImageCreateInfo /
//   isSameImageViewCreateInfo / SamplerDesc::operator== /
//   isSameImportedImageDesc.
// * Replacement always creates a new backend object and retires the old
//   owner through a completion-safe queue (e.g. DeferredDeletionQueue).
//   Backend resources never resize/mutate in place.
//
// Compat adapters with explicit deletion points:
// * Texture::createRenderTexture / Texture::getResourceFactory — deleted by
//   FG-803/FG-804 (texture upload service + factory removal).
// * FrameBuffer legacy Texture-backed views — deleted with the texture
//   attachment path (FG-804).
// * RenderGraphExecutor::getRegistry() — kept only for graph-internal sync
//   and low-level tests; graph-external owner escape removed by FG-105,
//   final deletion tracked by FG-901.
// ═══════════════════════════════════════════════════════════════════════

struct ImageViewCreateInfo
{
    std::string       label;
    EImageViewType::T viewType       = EImageViewType::View2D;
    EImageAspect::T   aspectFlags    = EImageAspect::None;
    uint32_t          baseMipLevel   = 0;
    uint32_t          levelCount     = 1;
    uint32_t          baseArrayLayer = 0;
    uint32_t          layerCount     = 1;
    ComponentMapping  components     = {};
};

struct ImageViewDescKey
{
    EImageViewType::T viewType       = EImageViewType::View2D;
    EImageAspect::T   aspectFlags    = EImageAspect::None;
    uint32_t          baseMipLevel   = 0;
    uint32_t          levelCount     = 1;
    uint32_t          baseArrayLayer = 0;
    uint32_t          layerCount     = 1;
    ComponentMapping  components     = {};
};

inline ImageViewDescKey makeImageViewDescKey(const ImageViewCreateInfo& desc)
{
    return ImageViewDescKey{
        .viewType       = desc.viewType,
        .aspectFlags    = desc.aspectFlags,
        .baseMipLevel   = desc.baseMipLevel,
        .levelCount     = desc.levelCount,
        .baseArrayLayer = desc.baseArrayLayer,
        .layerCount     = desc.layerCount,
        .components     = desc.components,
    };
}

inline bool isSameImageViewDescKey(const ImageViewDescKey& lhs, const ImageViewDescKey& rhs)
{
    return lhs.viewType == rhs.viewType &&
           lhs.aspectFlags == rhs.aspectFlags &&
           lhs.baseMipLevel == rhs.baseMipLevel &&
           lhs.levelCount == rhs.levelCount &&
           lhs.baseArrayLayer == rhs.baseArrayLayer &&
           lhs.layerCount == rhs.layerCount &&
           lhs.components.r == rhs.components.r &&
           lhs.components.g == rhs.components.g &&
           lhs.components.b == rhs.components.b &&
           lhs.components.a == rhs.components.a;
}

inline bool isSameImageViewCreateInfo(const ImageViewCreateInfo& lhs, const ImageViewCreateInfo& rhs)
{
    return lhs.label == rhs.label &&
           isSameImageViewDescKey(makeImageViewDescKey(lhs), makeImageViewDescKey(rhs));
}

enum class EImportedImageOwnership : uint8_t
{
    BorrowedNative = 0,
    OwnedNative,
};

struct ImportedImageDesc
{
    std::string             label;
    void*                   nativeHandle = nullptr;
    EFormat::T              format       = EFormat::Undefined;
    EImageUsage::T          usage        = EImageUsage::None;
    Extent3D                extent       = {};
    uint32_t                mipLevels    = 1;
    uint32_t                arrayLayers  = 1;
    EImportedImageOwnership ownership    = EImportedImageOwnership::BorrowedNative;
    EImageLayout::T         initialLayout = EImageLayout::Undefined;
    EImageLayout::T         finalLayout   = EImageLayout::Undefined;
};

inline bool isSameImportedImageDesc(const ImportedImageDesc& lhs, const ImportedImageDesc& rhs)
{
    return lhs.label == rhs.label &&
           lhs.nativeHandle == rhs.nativeHandle &&
           lhs.format == rhs.format &&
           lhs.usage == rhs.usage &&
           lhs.extent.width == rhs.extent.width &&
           lhs.extent.height == rhs.extent.height &&
           lhs.extent.depth == rhs.extent.depth &&
           lhs.mipLevels == rhs.mipLevels &&
           lhs.arrayLayers == rhs.arrayLayers &&
           lhs.ownership == rhs.ownership &&
           lhs.initialLayout == rhs.initialLayout &&
           lhs.finalLayout == rhs.finalLayout;
}

struct IRenderResourceFactory
{
    virtual ~IRenderResourceFactory() = default;

    virtual std::shared_ptr<IBuffer> createBuffer(const BufferCreateInfo& desc) = 0;
    virtual std::shared_ptr<Sampler> createSampler(const SamplerDesc& desc) = 0;
    virtual std::shared_ptr<IImage> createImage(const ImageCreateInfo& desc) = 0;
    virtual std::shared_ptr<IImage> importImage(const ImportedImageDesc& desc) = 0;
    virtual std::shared_ptr<IImageView> createImageView(
        std::shared_ptr<IImage> image,
        const ImageViewCreateInfo& desc) = 0;
};

} // namespace ya
