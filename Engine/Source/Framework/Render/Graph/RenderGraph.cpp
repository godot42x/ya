#include "RenderGraph.h"
#include "RenderGraphResourceRegistry.h"
#include "RHI/Core/RenderingInfoUtils.h"

#include <algorithm>
#include <deque>
#include <sstream>
#include <unordered_set>

namespace ya
{

void RenderGraphExecutionResult::bindExportedTexture(std::string name, std::shared_ptr<RenderTexture> texture)
{
    if (name.empty()) {
        return;
    }
    _exportedTextures[std::move(name)] = std::move(texture);
}

bool RenderGraphExecutionResult::hasExportedTexture(std::string_view name) const
{
    return _exportedTextures.contains(std::string(name));
}

std::shared_ptr<RenderTexture> RenderGraphExecutionResult::getExportedTextureShared(std::string_view name) const
{
    if (const auto it = _exportedTextures.find(std::string(name)); it != _exportedTextures.end()) {
        return it->second;
    }
    return nullptr;
}

RenderTexture* RenderGraphExecutionResult::getExportedTexture(std::string_view name) const
{
    if (const auto it = _exportedTextures.find(std::string(name)); it != _exportedTextures.end()) {
        return it->second.get();
    }
    return nullptr;
}

namespace
{

template <typename HandleT>
bool isHandleDeterministicallyBefore(const HandleT& lhs, const HandleT& rhs)
{
    if (lhs.index != rhs.index) {
        return lhs.index < rhs.index;
    }
    return lhs.generation < rhs.generation;
}

const char* toString(ERGPassResourceAccess access)
{
    switch (access) {
    case ERGPassResourceAccess::Read:            return "Read";
    case ERGPassResourceAccess::Write:           return "Write";
    case ERGPassResourceAccess::ColorAttachment: return "ColorAttachment";
    case ERGPassResourceAccess::DepthAttachment: return "DepthAttachment";
    case ERGPassResourceAccess::TransferSrc:     return "TransferSrc";
    case ERGPassResourceAccess::TransferDst:     return "TransferDst";
    }
    return "<unknown>";
}

const char* toString(ERGBufferAccess access)
{
    switch (access) {
    case ERGBufferAccess::UniformRead:      return "UniformRead";
    case ERGBufferAccess::StorageRead:      return "StorageRead";
    case ERGBufferAccess::StorageWrite:     return "StorageWrite";
    case ERGBufferAccess::StorageReadWrite: return "StorageReadWrite";
    case ERGBufferAccess::IndirectRead:     return "IndirectRead";
    case ERGBufferAccess::TransferRead:     return "TransferRead";
    case ERGBufferAccess::TransferWrite:    return "TransferWrite";
    }
    return "<unknown>";
}

template <typename UsageT>
bool containsUsage(const std::vector<UsageT>& usages, const UsageT& needle)
{
    return std::find(usages.begin(), usages.end(), needle) != usages.end();
}

bool hasImageUsage(EImageUsage::T haystack, EImageUsage::T needle)
{
    return (haystack & needle) == needle;
}

bool hasBufferUsage(EBufferUsage haystack, EBufferUsage needle)
{
    return (haystack & needle) == needle;
}

bool usesExplicitImportedTextureSubresource(const RGImportedTextureDesc& importedDesc)
{
    return importedDesc.subresourceRange.has_value() || importedDesc.viewDesc.has_value();
}

std::string formatImageUsageFlags(EImageUsage::T usage)
{
    if (usage == EImageUsage::None) {
        return "None";
    }

    std::vector<const char*> parts;
    if ((usage & EImageUsage::TransferSrc) == EImageUsage::TransferSrc) parts.push_back("TransferSrc");
    if ((usage & EImageUsage::TransferDst) == EImageUsage::TransferDst) parts.push_back("TransferDst");
    if ((usage & EImageUsage::Sampled) == EImageUsage::Sampled) parts.push_back("Sampled");
    if ((usage & EImageUsage::Storage) == EImageUsage::Storage) parts.push_back("Storage");
    if ((usage & EImageUsage::ColorAttachment) == EImageUsage::ColorAttachment) parts.push_back("ColorAttachment");
    if ((usage & EImageUsage::DepthStencilAttachment) == EImageUsage::DepthStencilAttachment) parts.push_back("DepthStencilAttachment");
    if ((usage & EImageUsage::TransientAttachment) == EImageUsage::TransientAttachment) parts.push_back("TransientAttachment");
    if ((usage & EImageUsage::InputAttachment) == EImageUsage::InputAttachment) parts.push_back("InputAttachment");

    std::ostringstream oss;
    for (size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            oss << "|";
        }
        oss << parts[index];
    }
    return oss.str();
}

std::string formatBufferUsageFlags(EBufferUsage usage)
{
    if (usage == EBufferUsage::None) {
        return "None";
    }

    std::vector<const char*> parts;
    if ((usage & EBufferUsage::TransferSrc) == EBufferUsage::TransferSrc) parts.push_back("TransferSrc");
    if ((usage & EBufferUsage::TransferDst) == EBufferUsage::TransferDst) parts.push_back("TransferDst");
    if ((usage & EBufferUsage::UniformTexelBuffer) == EBufferUsage::UniformTexelBuffer) parts.push_back("UniformTexelBuffer");
    if ((usage & EBufferUsage::StorageTexelBuffer) == EBufferUsage::StorageTexelBuffer) parts.push_back("StorageTexelBuffer");
    if ((usage & EBufferUsage::UniformBuffer) == EBufferUsage::UniformBuffer) parts.push_back("UniformBuffer");
    if ((usage & EBufferUsage::StorageBuffer) == EBufferUsage::StorageBuffer) parts.push_back("StorageBuffer");
    if ((usage & EBufferUsage::IndexBuffer) == EBufferUsage::IndexBuffer) parts.push_back("IndexBuffer");
    if ((usage & EBufferUsage::VertexBuffer) == EBufferUsage::VertexBuffer) parts.push_back("VertexBuffer");
    if ((usage & EBufferUsage::IndirectBuffer) == EBufferUsage::IndirectBuffer) parts.push_back("IndirectBuffer");

    std::ostringstream oss;
    for (size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            oss << "|";
        }
        oss << parts[index];
    }
    return oss.str();
}

bool isTransferTextureAccess(ERGPassResourceAccess access)
{
    return access == ERGPassResourceAccess::TransferSrc || access == ERGPassResourceAccess::TransferDst;
}

bool isTransferBufferAccess(ERGBufferAccess access)
{
    return access == ERGBufferAccess::TransferRead || access == ERGBufferAccess::TransferWrite;
}

RGBufferRange normalizeBufferRange(const RGBufferResource& resource, const RGBufferRange& range)
{
    const uint64_t bufferSize = resource.desc.size;
    RGBufferRange  normalized = range;
    if (normalized.size == 0) {
        YA_CORE_ASSERT(normalized.offset <= bufferSize,
                       "RenderGraph buffer range offset {} exceeds buffer {} size {}",
                       normalized.offset,
                       resource.desc.label,
                       bufferSize);
        normalized.size = bufferSize - normalized.offset;
    }

    YA_CORE_ASSERT(normalized.size > 0,
                   "RenderGraph buffer range for {} must be non-zero after normalization",
                   resource.desc.label);
    YA_CORE_ASSERT(normalized.offset + normalized.size <= bufferSize,
                   "RenderGraph buffer range [{}, {}) exceeds buffer {} size {}",
                   normalized.offset,
                   normalized.offset + normalized.size,
                   resource.desc.label,
                   bufferSize);
    return normalized;
}

bool rangesOverlap(const RGBufferRange& lhs, const RGBufferRange& rhs)
{
    return lhs.offset < rhs.offset + rhs.size && rhs.offset < lhs.offset + lhs.size;
}

const char* toString(ERGPassKind kind)
{
    switch (kind) {
    case ERGPassKind::Unknown: return "Unknown";
    case ERGPassKind::Raster:  return "Raster";
    case ERGPassKind::Compute: return "Compute";
    case ERGPassKind::Copy:    return "Copy";
    }
    return "Unknown";
}

uint32_t graphDefaultAspectMask(EFormat::T format)
{
    if (EFormat::isDepthStencilFormat(format)) {
        return EImageAspect::DepthStencil;
    }
    if (EFormat::isDepthFormat(format)) {
        return EImageAspect::Depth;
    }
    return EImageAspect::Color;
}

ImageSubresourceRange makeFullRange(const RGTextureDesc& desc)
{
    return ImageSubresourceRange{
        .aspectMask     = graphDefaultAspectMask(desc.format),
        .baseMipLevel   = 0,
        .levelCount     = desc.mipLevels,
        .baseArrayLayer = 0,
        .layerCount     = desc.arrayLayers,
    };
}

ImageSubresourceRange makeImportedViewRange(const RGTextureResource& resource)
{
    if (resource.imported && resource.imported->subresourceRange.has_value()) {
        return *resource.imported->subresourceRange;
    }
    if (resource.imported && resource.imported->viewDesc.has_value()) {
        const auto& viewDesc = *resource.imported->viewDesc;
        return ImageSubresourceRange{
            .aspectMask     = viewDesc.aspectFlags != EImageAspect::None ? viewDesc.aspectFlags : graphDefaultAspectMask(resource.desc.format),
            .baseMipLevel   = viewDesc.baseMipLevel,
            .levelCount     = viewDesc.levelCount,
            .baseArrayLayer = viewDesc.baseArrayLayer,
            .layerCount     = viewDesc.layerCount,
        };
    }
    return makeFullRange(resource.desc);
}

void normalizeImportedTextureDesc(RGImportedTextureDesc& importedDesc)
{
    auto validateRange = [](const IImage& image, const ImageSubresourceRange& range, std::string_view label)
    {
        YA_CORE_ASSERT(range.levelCount > 0 && range.layerCount > 0,
                       "Imported texture '{}' subresource range must be non-empty",
                       label);
        YA_CORE_ASSERT(range.baseMipLevel < image.getMipLevels(),
                       "Imported texture '{}' base mip {} exceeds backing mip count {}",
                       label,
                       range.baseMipLevel,
                       image.getMipLevels());
        YA_CORE_ASSERT(range.baseArrayLayer < image.getArrayLayers(),
                       "Imported texture '{}' base layer {} exceeds backing layer count {}",
                       label,
                       range.baseArrayLayer,
                       image.getArrayLayers());
        YA_CORE_ASSERT(range.baseMipLevel + range.levelCount <= image.getMipLevels(),
                       "Imported texture '{}' mip range [{}..{}) exceeds backing mip count {}",
                       label,
                       range.baseMipLevel,
                       range.baseMipLevel + range.levelCount,
                       image.getMipLevels());
        YA_CORE_ASSERT(range.baseArrayLayer + range.layerCount <= image.getArrayLayers(),
                       "Imported texture '{}' layer range [{}..{}) exceeds backing layer count {}",
                       label,
                       range.baseArrayLayer,
                       range.baseArrayLayer + range.layerCount,
                       image.getArrayLayers());
    };

    if (importedDesc.resource) {
        YA_CORE_ASSERT(importedDesc.resource->getImage() != nullptr,
                       "Imported texture '{}' requires a backing image",
                       importedDesc.desc.label);
        const IImage&          image        = *importedDesc.resource->getImage();
        const auto*            imageView    = importedDesc.resource->getImageView();
        const auto             nativeHandle = static_cast<void*>(image.getHandle());
        const std::string_view label        = importedDesc.importDesc.label.empty() ? importedDesc.desc.label : importedDesc.importDesc.label;

        YA_CORE_ASSERT(importedDesc.importDesc.nativeHandle == nullptr || importedDesc.importDesc.nativeHandle == nativeHandle,
                       "Imported texture image/native handle mismatch for '{}'",
                       label);
        YA_CORE_ASSERT(importedDesc.importDesc.format == EFormat::Undefined || importedDesc.importDesc.format == image.getFormat(),
                       "Imported texture image/format mismatch");
        YA_CORE_ASSERT(importedDesc.importDesc.usage == EImageUsage::None || hasImageUsage(image.getUsage(), importedDesc.importDesc.usage),
                       "Imported texture image/usage mismatch");
        YA_CORE_ASSERT((importedDesc.importDesc.extent.width == 0 && importedDesc.importDesc.extent.height == 0 && importedDesc.importDesc.extent.depth == 0) ||
                           (importedDesc.importDesc.extent.width == image.getWidth() &&
                            importedDesc.importDesc.extent.height == image.getHeight() &&
                            importedDesc.importDesc.extent.depth == 1),
                       "Imported texture image/extent mismatch");
        YA_CORE_ASSERT(importedDesc.importDesc.mipLevels == 1 || importedDesc.importDesc.mipLevels == image.getMipLevels(),
                       "Imported texture image/mip mismatch");
        YA_CORE_ASSERT(importedDesc.importDesc.arrayLayers == 1 || importedDesc.importDesc.arrayLayers == image.getArrayLayers(),
                       "Imported texture image/array-layer mismatch");

        if (imageView) {
            YA_CORE_ASSERT(imageView->getImage() == &image,
                           "Imported texture '{}' image view does not reference the backing image",
                           label);
        }
        if (importedDesc.subresourceRange) {
            validateRange(image, *importedDesc.subresourceRange, label);
        }
        if (imageView && importedDesc.subresourceRange) {
            const auto& viewRange = imageView->getSubresourceRange();
            YA_CORE_ASSERT(viewRange.aspectMask == importedDesc.subresourceRange->aspectMask &&
                               viewRange.baseMipLevel == importedDesc.subresourceRange->baseMipLevel &&
                               viewRange.levelCount == importedDesc.subresourceRange->levelCount &&
                               viewRange.baseArrayLayer == importedDesc.subresourceRange->baseArrayLayer &&
                               viewRange.layerCount == importedDesc.subresourceRange->layerCount,
                           "Imported texture '{}' image-view range does not match declared subresource range",
                           label);
        }

        importedDesc.importDesc.nativeHandle = nativeHandle;
        importedDesc.importDesc.format       = image.getFormat();
        importedDesc.importDesc.usage        = image.getUsage();
        importedDesc.importDesc.extent       = Extent3D{image.getWidth(), image.getHeight(), 1};
        importedDesc.importDesc.mipLevels    = image.getMipLevels();
        importedDesc.importDesc.arrayLayers  = image.getArrayLayers();
    }

    auto& desc = importedDesc.desc;
    if (desc.format == EFormat::Undefined) {
        desc.format = importedDesc.importDesc.format;
    }
    if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
        desc.extent = importedDesc.importDesc.extent;
    }
    if (desc.usage == EImageUsage::None) {
        desc.usage = importedDesc.importDesc.usage;
    }
    if (desc.label.empty()) {
        desc.label = importedDesc.importDesc.label;
    }
    const bool bUsesExplicitSubresource = usesExplicitImportedTextureSubresource(importedDesc);
    if (!bUsesExplicitSubresource && desc.mipLevels == 1 && importedDesc.importDesc.mipLevels != 1) {
        desc.mipLevels = importedDesc.importDesc.mipLevels;
    }
    if (!bUsesExplicitSubresource && desc.arrayLayers == 1 && importedDesc.importDesc.arrayLayers != 1) {
        desc.arrayLayers = importedDesc.importDesc.arrayLayers;
    }
    if (importedDesc.subresourceRange) {
        YA_CORE_ASSERT(desc.mipLevels == importedDesc.subresourceRange->levelCount,
                       "Imported texture '{}' graph mip count {} does not match imported subresource level count {}",
                       desc.label,
                       desc.mipLevels,
                       importedDesc.subresourceRange->levelCount);
        YA_CORE_ASSERT(desc.arrayLayers == importedDesc.subresourceRange->layerCount,
                       "Imported texture '{}' graph layer count {} does not match imported subresource layer count {}",
                       desc.label,
                       desc.arrayLayers,
                       importedDesc.subresourceRange->layerCount);
    }

    if (importedDesc.resource && importedDesc.resource->getImage() != nullptr) {
        const EImageUsage::T backingUsage = importedDesc.resource->getImage()->getUsage();
        YA_CORE_ASSERT(hasImageUsage(backingUsage, desc.usage),
                       "Imported texture '{}' graph usage {} is not supported by backing image usage {}",
                       desc.label,
                       formatImageUsageFlags(desc.usage),
                       formatImageUsageFlags(backingUsage));
    }
}

void normalizeImportedBufferDesc(RGImportedBufferDesc& importedDesc)
{
    YA_CORE_ASSERT(importedDesc.buffer != nullptr, "Imported buffer requires a backing buffer");

    auto& desc = importedDesc.desc;
    if (desc.label.empty()) {
        desc.label = importedDesc.buffer->getName();
    }
    if (desc.size == 0) {
        desc.size = importedDesc.buffer->getSize();
    }
    if (desc.usage == EBufferUsage::None) {
        desc.usage = importedDesc.buffer->getUsage();
    }

    YA_CORE_ASSERT(desc.size == importedDesc.buffer->getSize(),
                   "Imported buffer '{}' size mismatch: graph desc={} backing={}",
                   desc.label,
                   desc.size,
                   importedDesc.buffer->getSize());
    YA_CORE_ASSERT(hasBufferUsage(importedDesc.buffer->getUsage(), desc.usage),
                   "Imported buffer '{}' graph usage {} is not supported by backing buffer usage {}",
                   desc.label,
                   formatBufferUsageFlags(desc.usage),
                   formatBufferUsageFlags(importedDesc.buffer->getUsage()));
}

ImageResourceState makeTextureState(const RGTextureResource& resource, ERGPassResourceAccess access)
{
    ImageResourceState state{
        .subresourceRange = makeImportedViewRange(resource),
    };

    switch (access) {
    case ERGPassResourceAccess::Read:
        state.stages = EPipelineStage::FragmentShader;
        state.access = EResourceAccess::ShaderRead;
        state.layout = EImageLayout::ShaderReadOnlyOptimal;
        break;
    case ERGPassResourceAccess::Write:
        state.stages = EPipelineStage::ComputeShader;
        state.access = EResourceAccess::ShaderWrite;
        state.layout = EImageLayout::General;
        break;
    case ERGPassResourceAccess::ColorAttachment:
        state.stages = EPipelineStage::ColorAttachmentOutput;
        state.access = EResourceAccess::ColorAttachmentWrite;
        state.layout = EImageLayout::ColorAttachmentOptimal;
        break;
    case ERGPassResourceAccess::DepthAttachment:
        state.stages = static_cast<EPipelineStage::T>(EPipelineStage::EarlyFragmentTests | EPipelineStage::LateFragmentTests);
        state.access = static_cast<EResourceAccess::T>(
            EResourceAccess::DepthStencilAttachmentRead | EResourceAccess::DepthStencilAttachmentWrite);
        state.layout = EImageLayout::DepthStencilAttachmentOptimal;
        break;
    case ERGPassResourceAccess::TransferSrc:
        state.stages = EPipelineStage::Transfer;
        state.access = EResourceAccess::TransferRead;
        state.layout = EImageLayout::TransferSrc;
        break;
    case ERGPassResourceAccess::TransferDst:
        state.stages = EPipelineStage::Transfer;
        state.access = EResourceAccess::TransferWrite;
        state.layout = EImageLayout::TransferDst;
        break;
    }

    return state;
}

BufferResourceState makeBufferState(ERGPassKind passKind, const RGBufferResource& resource, const RGBufferUsage& usage)
{
    const auto          normalizedRange = normalizeBufferRange(resource, usage.range);
    BufferResourceState state{
        .offset = normalizedRange.offset,
        .size   = normalizedRange.size,
    };

    switch (usage.access) {
    case ERGBufferAccess::UniformRead:
        state.stages = EPipelineStage::AllCommands;
        state.access = EResourceAccess::ShaderRead;
        break;
    case ERGBufferAccess::StorageRead:
        state.stages = passKind == ERGPassKind::Compute ? EPipelineStage::ComputeShader : EPipelineStage::AllCommands;
        state.access = EResourceAccess::ShaderRead;
        break;
    case ERGBufferAccess::StorageWrite:
        state.stages = passKind == ERGPassKind::Compute ? EPipelineStage::ComputeShader : EPipelineStage::AllCommands;
        state.access = EResourceAccess::ShaderWrite;
        break;
    case ERGBufferAccess::StorageReadWrite:
        state.stages = passKind == ERGPassKind::Compute ? EPipelineStage::ComputeShader : EPipelineStage::AllCommands;
        state.access = static_cast<EResourceAccess::T>(EResourceAccess::ShaderRead | EResourceAccess::ShaderWrite);
        break;
    case ERGBufferAccess::IndirectRead:
        state.stages = EPipelineStage::DrawIndirect;
        state.access = EResourceAccess::IndirectCommandRead;
        break;
    case ERGBufferAccess::TransferRead:
        state.stages = EPipelineStage::Transfer;
        state.access = EResourceAccess::TransferRead;
        break;
    case ERGBufferAccess::TransferWrite:
        state.stages = EPipelineStage::Transfer;
        state.access = EResourceAccess::TransferWrite;
        break;
    }

    return state;
}

bool isBufferReadAccess(ERGBufferAccess access)
{
    switch (access) {
    case ERGBufferAccess::UniformRead:
    case ERGBufferAccess::StorageRead:
    case ERGBufferAccess::StorageReadWrite:
    case ERGBufferAccess::IndirectRead:
    case ERGBufferAccess::TransferRead:
        return true;
    case ERGBufferAccess::StorageWrite:
    case ERGBufferAccess::TransferWrite:
        return false;
    }
    return false;
}

bool isBufferWriteAccess(ERGBufferAccess access)
{
    switch (access) {
    case ERGBufferAccess::StorageWrite:
    case ERGBufferAccess::StorageReadWrite:
    case ERGBufferAccess::TransferWrite:
        return true;
    case ERGBufferAccess::UniformRead:
    case ERGBufferAccess::StorageRead:
    case ERGBufferAccess::IndirectRead:
    case ERGBufferAccess::TransferRead:
        return false;
    }
    return false;
}

bool transientLifetimesOverlap(const RGTransientBufferLifetimePlan& lhs,
                               const RGTransientBufferLifetimePlan& rhs)
{
    return lhs.isUsed() && rhs.isUsed() &&
           lhs.firstPassIndex <= rhs.lastPassIndex &&
           rhs.firstPassIndex <= lhs.lastPassIndex;
}

void appendBufferUsage(RGPass& pass, RGBufferHandle handle, ERGBufferAccess access, RGBufferRange range)
{
    pass.buffers.push_back({
        .handle = handle,
        .access = access,
        .range  = range,
    });
}

bool isUniformOnlyBufferUsage(EBufferUsage usage)
{
    return hasBufferUsage(usage, EBufferUsage::UniformBuffer) &&
           !hasBufferUsage(usage, EBufferUsage::StorageBuffer);
}

bool isStorageCapableBufferUsage(EBufferUsage usage)
{
    return hasBufferUsage(usage, EBufferUsage::StorageBuffer);
}

bool isUniformCapableBufferUsage(EBufferUsage usage)
{
    return hasBufferUsage(usage, EBufferUsage::UniformBuffer);
}

bool validateBufferUsageFlags(const RGBufferResource& resource, ERGBufferAccess access)
{
    switch (access) {
    case ERGBufferAccess::UniformRead:
        return isUniformCapableBufferUsage(resource.desc.usage);
    case ERGBufferAccess::StorageRead:
    case ERGBufferAccess::StorageWrite:
    case ERGBufferAccess::StorageReadWrite:
        return isStorageCapableBufferUsage(resource.desc.usage);
    case ERGBufferAccess::IndirectRead:
        return hasBufferUsage(resource.desc.usage, EBufferUsage::IndirectBuffer);
    case ERGBufferAccess::TransferRead:
        return hasBufferUsage(resource.desc.usage, EBufferUsage::TransferSrc);
    case ERGBufferAccess::TransferWrite:
        return hasBufferUsage(resource.desc.usage, EBufferUsage::TransferDst);
    }
    return false;
}

std::string describeRequiredImageUsage(ERGPassResourceAccess access)
{
    switch (access) {
    case ERGPassResourceAccess::Read:            return "Sampled";
    case ERGPassResourceAccess::Write:           return "Storage";
    case ERGPassResourceAccess::ColorAttachment: return "ColorAttachment";
    case ERGPassResourceAccess::DepthAttachment: return "DepthStencilAttachment";
    case ERGPassResourceAccess::TransferSrc:     return "TransferSrc";
    case ERGPassResourceAccess::TransferDst:     return "TransferDst";
    }
    return "<unknown>";
}

std::string describeRequiredBufferUsage(ERGBufferAccess access)
{
    switch (access) {
    case ERGBufferAccess::UniformRead: return "UniformBuffer";
    case ERGBufferAccess::StorageRead:
    case ERGBufferAccess::StorageWrite:
    case ERGBufferAccess::StorageReadWrite:
        return "StorageBuffer";
    case ERGBufferAccess::IndirectRead:  return "IndirectBuffer";
    case ERGBufferAccess::TransferRead:  return "TransferSrc";
    case ERGBufferAccess::TransferWrite: return "TransferDst";
    }
    return "<unknown>";
}

std::string formatBufferRange(const RGBufferRange& range)
{
    return std::format("[{}, {})", range.offset, range.offset + range.size);
}

void retainResolvedRenderTexture(ICommandBuffer& cmdBuf, const RenderTexture& image)
{
    cmdBuf.retainResource(image.getImageShared());
    cmdBuf.retainResource(image.getImageViewShared());
    cmdBuf.retainResources(image.getRetainedResources());
}

} // namespace

const RGTextureResource& RGPassContext::getTexture(RGTextureHandle handle) const
{
    const auto* resource = _graph.getTexture(handle);
    YA_CORE_ASSERT(resource != nullptr, "RGPassContext pass {} references invalid texture handle {}", _pass.name, handle.index);
    return *resource;
}

const RGBufferResource& RGPassContext::getBuffer(RGBufferHandle handle) const
{
    const auto* resource = _graph.getBuffer(handle);
    YA_CORE_ASSERT(resource != nullptr, "RGPassContext pass {} references invalid buffer handle {}", _pass.name, handle.index);
    return *resource;
}

const RGTextureDesc& RGPassContext::getTextureDesc(RGTextureHandle handle) const
{
    return getTexture(handle).desc;
}

const RGBufferDesc& RGPassContext::getBufferDesc(RGBufferHandle handle) const
{
    return getBuffer(handle).desc;
}

const RGTextureResource& RGRenderContext::getTexture(RGTextureHandle handle) const
{
    const auto* resource = _graph.getTexture(handle);
    YA_CORE_ASSERT(resource != nullptr, "RGRenderContext pass {} references invalid texture handle {}", _pass.name, handle.index);
    return *resource;
}

const RGBufferResource& RGRenderContext::getBuffer(RGBufferHandle handle) const
{
    const auto* resource = _graph.getBuffer(handle);
    YA_CORE_ASSERT(resource != nullptr, "RGRenderContext pass {} references invalid buffer handle {}", _pass.name, handle.index);
    return *resource;
}

const RGTextureDesc& RGRenderContext::getTextureDesc(RGTextureHandle handle) const
{
    return getTexture(handle).desc;
}

const RGBufferDesc& RGRenderContext::getBufferDesc(RGBufferHandle handle) const
{
    return getBuffer(handle).desc;
}

bool RGRenderContext::hasDeclaredTextureUsage(RGTextureHandle handle) const
{
    return findDeclaredTextureUsage(handle) != nullptr;
}

bool RGRenderContext::hasDeclaredBufferUsage(RGBufferHandle handle) const
{
    return findDeclaredBufferUsage(handle) != nullptr;
}

bool RGRenderContext::hasDeclaredTextureAccess(RGTextureHandle handle, ERGPassResourceAccess access) const
{
    const auto* usage = findDeclaredTextureUsage(handle);
    return usage != nullptr && usage->access == access;
}

bool RGRenderContext::hasDeclaredBufferAccess(RGBufferHandle handle, ERGBufferAccess access) const
{
    const auto* usage = findDeclaredBufferUsage(handle);
    return usage != nullptr && usage->access == access;
}

const RGTextureUsage* RGRenderContext::findDeclaredTextureUsage(RGTextureHandle handle) const
{
    const auto it = std::find_if(_pass.textures.begin(),
                                 _pass.textures.end(),
                                 [handle](const RGTextureUsage& usage)
                                 { return usage.handle == handle; });
    return it != _pass.textures.end() ? &*it : nullptr;
}

const RGBufferUsage* RGRenderContext::findDeclaredBufferUsage(RGBufferHandle handle) const
{
    const auto it = std::find_if(_pass.buffers.begin(),
                                 _pass.buffers.end(),
                                 [handle](const RGBufferUsage& usage)
                                 { return usage.handle == handle; });
    return it != _pass.buffers.end() ? &*it : nullptr;
}

void RGRenderContext::assertTextureDeclared(RGTextureHandle handle, const char* operation) const
{
    const auto* usage = findDeclaredTextureUsage(handle);
    YA_CORE_ASSERT(usage != nullptr,
                   "RGRenderContext pass {} attempted {} on undeclared texture handle {}",
                   _pass.name,
                   operation,
                   handle.index);
}

void RGRenderContext::assertBufferDeclared(RGBufferHandle handle, const char* operation) const
{
    const auto* usage = findDeclaredBufferUsage(handle);
    YA_CORE_ASSERT(usage != nullptr,
                   "RGRenderContext pass {} attempted {} on undeclared buffer handle {}",
                   _pass.name,
                   operation,
                   handle.index);
}

void RGRenderContext::assertTextureAccess(RGTextureHandle                              handle,
                                          std::initializer_list<ERGPassResourceAccess> allowed,
                                          const char*                                  operation) const
{
    const auto* usage = findDeclaredTextureUsage(handle);
    YA_CORE_ASSERT(usage != nullptr,
                   "RGRenderContext pass {} attempted {} on undeclared texture handle {}",
                   _pass.name,
                   operation,
                   handle.index);
    const bool matched = std::find(allowed.begin(), allowed.end(), usage->access) != allowed.end();
    YA_CORE_ASSERT(matched,
                   "RGRenderContext pass {} attempted {} on texture handle {} declared as {}",
                   _pass.name,
                   operation,
                   handle.index,
                   toString(usage->access));
}

void RGRenderContext::assertBufferAccess(RGBufferHandle                         handle,
                                         std::initializer_list<ERGBufferAccess> allowed,
                                         const char*                            operation) const
{
    const auto* usage = findDeclaredBufferUsage(handle);
    YA_CORE_ASSERT(usage != nullptr,
                   "RGRenderContext pass {} attempted {} on undeclared buffer handle {}",
                   _pass.name,
                   operation,
                   handle.index);
    const bool matched = std::find(allowed.begin(), allowed.end(), usage->access) != allowed.end();
    YA_CORE_ASSERT(matched,
                   "RGRenderContext pass {} attempted {} on buffer handle {} declared as {}",
                   _pass.name,
                   operation,
                   handle.index,
                   toString(usage->access));
}

const RenderTexture* RGRenderContext::resolveTexture(RGTextureHandle handle) const
{
    assertTextureDeclared(handle, "resolveTexture");
    const auto* texture = _registry.resolveTexture(handle);
    if (texture) {
        retainResolvedRenderTexture(_cmdBuf, *texture);
    }
    return texture;
}

IBuffer* RGRenderContext::resolveBuffer(RGBufferHandle handle) const
{
    assertBufferDeclared(handle, "resolveBuffer");
    const auto* resource = _graph.getBuffer(handle);
    YA_CORE_ASSERT(resource != nullptr, "RGRenderContext pass {} references invalid buffer handle {}", _pass.name, handle.index);
    if (resource->imported.has_value()) {
        _cmdBuf.retainResources(resource->imported->retainedResources);
    }
    return _registry.resolveBuffer(handle);
}

const RGRasterPassDesc* RGRenderContext::getDeclaredRasterPlan() const
{
    if (_compiledPassPlan == nullptr || !_compiledPassPlan->rasterPlan.has_value()) {
        return nullptr;
    }
    return &*_compiledPassPlan->rasterPlan;
}

RGRenderContext::RasterPassExecutionParams RGRenderContext::getRasterPassExecutionParams() const
{
    const auto* rasterPlan = getDeclaredRasterPlan();
    YA_CORE_ASSERT(rasterPlan != nullptr,
                   "RGRenderContext pass {} has no declared raster execution params",
                   _pass.name);
    return RasterPassExecutionParams{.rasterPlan = *rasterPlan};
}

void RGRenderContext::beginColorRendering(const ColorRenderingDesc& desc) const
{
    beginRasterRendering(RasterRenderingDesc{
        .renderArea = desc.renderArea,
        .layerCount = desc.layerCount,
        .colors     = {{
                .color       = desc.color,
                .clearValue  = desc.clearValue,
                .loadOp      = desc.loadOp,
                .storeOp     = desc.storeOp,
                .finalLayout = desc.finalLayout,
        }},
    });
}

void RGRenderContext::beginDeclaredRasterRendering() const
{
    YA_CORE_ASSERT(_compiledPassPlan != nullptr,
                   "RGRenderContext pass {} is missing compiled pass plan for declared raster rendering",
                   _pass.name);
    YA_CORE_ASSERT(_compiledPassPlan->rasterPlan.has_value(),
                   "RGRenderContext pass {} has no declared raster rendering plan",
                   _pass.name);
    beginRasterRendering(*_compiledPassPlan->rasterPlan);
}

void RGRenderContext::beginRasterRendering(const RasterRenderingDesc& desc) const
{
    YA_CORE_ASSERT(!desc.colors.empty() || desc.depth.has_value(),
                   "RGRenderContext pass {} requires at least one attachment",
                   _pass.name);

    RenderAttachmentSet attachments{
        .renderArea = desc.renderArea,
        .layerCount = desc.layerCount,
    };
    attachments.colors.reserve(desc.colors.size());

    for (const auto& colorDesc : desc.colors) {
        const auto* color = resolveTexture(colorDesc.color);
        YA_CORE_ASSERT(color != nullptr, "RGRenderContext pass {} failed to resolve color target {}", _pass.name, colorDesc.color.index);
        YA_CORE_ASSERT(color->getImage() != nullptr && color->getImageView() != nullptr,
                       "RGRenderContext pass {} color target {} is missing image/view",
                       _pass.name,
                       colorDesc.color.index);
        retainResolvedRenderTexture(_cmdBuf, *color);
        auto attachment = makeRenderAttachment(
            color->getImageView(),
            colorDesc.loadOp,
            colorDesc.storeOp,
            EImageLayout::ColorAttachmentOptimal,
            colorDesc.finalLayout,
            colorDesc.clearValue);
        if (colorDesc.resolve.isValid()) {
            const auto* resolve = resolveTexture(colorDesc.resolve);
            YA_CORE_ASSERT(resolve != nullptr,
                           "RGRenderContext pass {} failed to resolve color-resolve target {}",
                           _pass.name,
                           colorDesc.resolve.index);
            YA_CORE_ASSERT(resolve->getImage() != nullptr && resolve->getImageView() != nullptr,
                           "RGRenderContext pass {} color-resolve target {} is missing image/view",
                           _pass.name,
                           colorDesc.resolve.index);
            retainResolvedRenderTexture(_cmdBuf, *resolve);
            attachment.resolveImage     = resolve->getImage();
            attachment.resolveImageView = resolve->getImageView();
            attachment.resolveMode      = colorDesc.resolveMode;
        }
        attachments.colors.push_back(std::move(attachment));
    }

    if (desc.depth.has_value()) {
        const auto* depth = resolveTexture(desc.depth->depth);
        YA_CORE_ASSERT(depth != nullptr, "RGRenderContext pass {} failed to resolve depth target {}", _pass.name, desc.depth->depth.index);
        YA_CORE_ASSERT(depth->getImage() != nullptr && depth->getImageView() != nullptr,
                       "RGRenderContext pass {} depth target {} is missing image/view",
                       _pass.name,
                       desc.depth->depth.index);
        retainResolvedRenderTexture(_cmdBuf, *depth);

        attachments.depth = makeRenderAttachment(
            depth->getImageView(),
            desc.depth->loadOp,
            desc.depth->storeOp,
            EImageLayout::DepthStencilAttachmentOptimal,
            desc.depth->finalLayout,
            desc.depth->clearValue);
    }

    _activeRenderingInfo = RenderingInfo{
        .label       = _pass.name,
        .attachments = std::move(attachments),
    };
    _cmdBuf.beginRendering(*_activeRenderingInfo);
}

void RGRenderContext::endRendering() const
{
    _cmdBuf.endRendering(_activeRenderingInfo.value_or(RenderingInfo{}));
    _activeRenderingInfo.reset();
}

void RGRenderContext::copyBuffer(RGBufferHandle src, RGBufferHandle dst, uint64_t size, uint64_t srcOffset, uint64_t dstOffset) const
{
    assertBufferAccess(src, {ERGBufferAccess::TransferRead}, "copyBuffer(src)");
    assertBufferAccess(dst, {ERGBufferAccess::TransferWrite}, "copyBuffer(dst)");
    auto* srcBuffer = resolveBuffer(src);
    auto* dstBuffer = resolveBuffer(dst);
    YA_CORE_ASSERT(srcBuffer != nullptr, "RGRenderContext pass {} failed to resolve src buffer {}", _pass.name, src.index);
    YA_CORE_ASSERT(dstBuffer != nullptr, "RGRenderContext pass {} failed to resolve dst buffer {}", _pass.name, dst.index);
    _cmdBuf.copyBuffer(srcBuffer, dstBuffer, size, srcOffset, dstOffset);
}

void RGRenderContext::copyTextureToBuffer(
    RGTextureHandle                     src,
    RGBufferHandle                      dst,
    const std::vector<BufferImageCopy>& regions) const
{
    assertTextureAccess(src, {ERGPassResourceAccess::TransferSrc}, "copyTextureToBuffer(src)");
    assertBufferAccess(dst, {ERGBufferAccess::TransferWrite}, "copyTextureToBuffer(dst)");
    const auto* srcTexture = resolveTexture(src);
    auto*       dstBuffer  = resolveBuffer(dst);
    YA_CORE_ASSERT(srcTexture != nullptr, "RGRenderContext pass {} failed to resolve src texture {}", _pass.name, src.index);
    YA_CORE_ASSERT(dstBuffer != nullptr, "RGRenderContext pass {} failed to resolve dst buffer {}", _pass.name, dst.index);
    YA_CORE_ASSERT(srcTexture->getImage() != nullptr, "RGRenderContext pass {} src texture {} is missing image", _pass.name, src.index);
    retainResolvedRenderTexture(_cmdBuf, *srcTexture);

    _cmdBuf.copyImageToBuffer(
        srcTexture->getImage(),
        EImageLayout::TransferSrc,
        dstBuffer,
        regions);
}

void RGRenderContext::copyTexture(RGTextureHandle src, RGTextureHandle dst, const ImageCopy& region) const
{
    assertTextureAccess(src, {ERGPassResourceAccess::TransferSrc}, "copyTexture(src)");
    assertTextureAccess(dst, {ERGPassResourceAccess::TransferDst}, "copyTexture(dst)");
    const auto* srcTexture = resolveTexture(src);
    const auto* dstTexture = resolveTexture(dst);
    YA_CORE_ASSERT(srcTexture != nullptr, "RGRenderContext pass {} failed to resolve src texture {}", _pass.name, src.index);
    YA_CORE_ASSERT(dstTexture != nullptr, "RGRenderContext pass {} failed to resolve dst texture {}", _pass.name, dst.index);
    YA_CORE_ASSERT(srcTexture->getImage() != nullptr, "RGRenderContext pass {} src texture {} is missing image", _pass.name, src.index);
    YA_CORE_ASSERT(dstTexture->getImage() != nullptr, "RGRenderContext pass {} dst texture {} is missing image", _pass.name, dst.index);
    retainResolvedRenderTexture(_cmdBuf, *srcTexture);
    retainResolvedRenderTexture(_cmdBuf, *dstTexture);

    _cmdBuf.copyImage(
        srcTexture->getImage(),
        EImageLayout::TransferSrc,
        dstTexture->getImage(),
        EImageLayout::TransferDst,
        {region});
}

RGPass& RGPassBuilder::pass()
{
    return const_cast<RGPass&>(_graph.getPasses()[_passIndex]);
}

void RGPassBuilder::read(RGTextureHandle handle)
{
    pass().textures.push_back({.handle = handle, .access = ERGPassResourceAccess::Read});
}

void RGPassBuilder::write(RGTextureHandle handle)
{
    pass().textures.push_back({.handle = handle, .access = ERGPassResourceAccess::Write});
}

void RGPassBuilder::uniformRead(RGBufferHandle handle, RGBufferRange range)
{
    appendBufferUsage(pass(), handle, ERGBufferAccess::UniformRead, range);
}

void RGPassBuilder::storageRead(RGBufferHandle handle, RGBufferRange range)
{
    appendBufferUsage(pass(), handle, ERGBufferAccess::StorageRead, range);
}

void RGPassBuilder::storageWrite(RGBufferHandle handle, RGBufferRange range)
{
    appendBufferUsage(pass(), handle, ERGBufferAccess::StorageWrite, range);
}

void RGPassBuilder::storageReadWrite(RGBufferHandle handle, RGBufferRange range)
{
    appendBufferUsage(pass(), handle, ERGBufferAccess::StorageReadWrite, range);
}

void RGPassBuilder::indirectRead(RGBufferHandle handle, RGBufferRange range)
{
    appendBufferUsage(pass(), handle, ERGBufferAccess::IndirectRead, range);
}

void RGPassBuilder::transferSrc(RGBufferHandle handle, RGBufferRange range)
{
    appendBufferUsage(pass(), handle, ERGBufferAccess::TransferRead, range);
}

void RGPassBuilder::transferDst(RGBufferHandle handle, RGBufferRange range)
{
    appendBufferUsage(pass(), handle, ERGBufferAccess::TransferWrite, range);
}

void RGPassBuilder::dependsOn(RGPassHandle handle)
{
    pass().dependencies.push_back(handle);
}

void RGPassBuilder::declareCompute()
{
    pass().kind = ERGPassKind::Compute;
}

void RGPassBuilder::declareCopy()
{
    pass().kind = ERGPassKind::Copy;
}

void RGPassBuilder::declareRaster(const RGRasterPassDesc& desc)
{
    auto& currentPass      = pass();
    currentPass.kind       = ERGPassKind::Raster;
    currentPass.rasterDesc = desc;

    for (const auto& color : desc.colors) {
        currentPass.textures.push_back({.handle = color.color, .access = ERGPassResourceAccess::ColorAttachment});
        if (color.resolve.isValid()) {
            currentPass.textures.push_back({.handle = color.resolve, .access = ERGPassResourceAccess::ColorAttachment});
        }
    }

    if (desc.depth.has_value()) {
        currentPass.textures.push_back({.handle = desc.depth->depth, .access = ERGPassResourceAccess::DepthAttachment});
    }
}

void RGPassBuilder::useColorAttachment(RGTextureHandle handle)
{
    pass().textures.push_back({.handle = handle, .access = ERGPassResourceAccess::ColorAttachment});
}

void RGPassBuilder::useDepthAttachment(RGTextureHandle handle)
{
    pass().textures.push_back({.handle = handle, .access = ERGPassResourceAccess::DepthAttachment});
}

void RGPassBuilder::transferSrc(RGTextureHandle handle)
{
    pass().textures.push_back({.handle = handle, .access = ERGPassResourceAccess::TransferSrc});
}

void RGPassBuilder::transferDst(RGTextureHandle handle)
{
    pass().textures.push_back({.handle = handle, .access = ERGPassResourceAccess::TransferDst});
}

RGTextureHandle RenderGraph::createTexture(const RGTextureDesc& desc, ERGResourceLifetime lifetime)
{
    YA_CORE_ASSERT(lifetime != ERGResourceLifetime::Imported, "Imported texture must use importTexture()");
    YA_CORE_ASSERT(desc.format != EFormat::Undefined, "RenderGraph texture desc format must be defined");
    YA_CORE_ASSERT(desc.extent.width > 0 && desc.extent.height > 0 && desc.extent.depth > 0,
                   "RenderGraph texture extent must be non-zero");

    RGTextureHandle handle{
        .index      = static_cast<uint32_t>(_textures.size()),
        .generation = _nextTextureGeneration++,
    };
    _textures.push_back(RGTextureResource{
        .handle   = handle,
        .lifetime = lifetime,
        .desc     = desc,
    });
    return handle;
}

RGTextureHandle RenderGraph::createPersistentTexture(const RGTextureDesc& desc, const RGPersistentTextureKey& key)
{
    YA_CORE_ASSERT(key.isValid(), "Persistent texture key must not be empty for '{}'", desc.label);

    const auto handle              = createTexture(desc, ERGResourceLifetime::Persistent);
    _textures.back().persistentKey = key;
    return handle;
}

RGTextureHandle RenderGraph::importTexture(const RGImportedTextureDesc& importedDesc)
{
    YA_CORE_ASSERT(importedDesc.resource || importedDesc.importDesc.nativeHandle != nullptr,
                   "Imported texture requires either image resource or native handle");

    auto normalizedImported = importedDesc;
    normalizeImportedTextureDesc(normalizedImported);

    RGTextureHandle handle{
        .index      = static_cast<uint32_t>(_textures.size()),
        .generation = _nextTextureGeneration++,
    };
    _textures.push_back(RGTextureResource{
        .handle   = handle,
        .lifetime = ERGResourceLifetime::Imported,
        .desc     = normalizedImported.desc,
        .imported = normalizedImported,
    });
    return handle;
}

RGBufferHandle RenderGraph::createBuffer(const RGBufferDesc& desc, ERGResourceLifetime lifetime)
{
    YA_CORE_ASSERT(lifetime != ERGResourceLifetime::Imported, "Imported buffer must use importBuffer()");
    YA_CORE_ASSERT(desc.size > 0, "RenderGraph buffer size must be non-zero");
    YA_CORE_ASSERT(desc.alignment > 0, "RenderGraph buffer alignment must be non-zero");

    RGBufferHandle handle{
        .index      = static_cast<uint32_t>(_buffers.size()),
        .generation = _nextBufferGeneration++,
    };
    _buffers.push_back(RGBufferResource{
        .handle   = handle,
        .lifetime = lifetime,
        .desc     = desc,
    });
    return handle;
}

RGBufferHandle RenderGraph::createPersistentBuffer(const RGBufferDesc& desc, const RGPersistentBufferKey& key)
{
    YA_CORE_ASSERT(key.isValid(), "Persistent buffer key must not be empty for '{}'", desc.label);

    const auto handle             = createBuffer(desc, ERGResourceLifetime::Persistent);
    _buffers.back().persistentKey = key;
    return handle;
}

RGBufferHandle RenderGraph::importBuffer(const RGImportedBufferDesc& importedDesc)
{
    YA_CORE_ASSERT(importedDesc.buffer != nullptr, "Imported buffer must not be null");

    auto normalizedImported = importedDesc;
    normalizeImportedBufferDesc(normalizedImported);

    RGBufferHandle handle{
        .index      = static_cast<uint32_t>(_buffers.size()),
        .generation = _nextBufferGeneration++,
    };
    _buffers.push_back(RGBufferResource{
        .handle   = handle,
        .lifetime = ERGResourceLifetime::Imported,
        .desc     = normalizedImported.desc,
        .imported = normalizedImported,
    });
    return handle;
}

const RGTextureResource* RenderGraph::getTexture(RGTextureHandle handle) const
{
    return findResource(_textures, handle);
}

const RGBufferResource* RenderGraph::getBuffer(RGBufferHandle handle) const
{
    return findResource(_buffers, handle);
}

const RGPass* RenderGraph::getPass(RGPassHandle handle) const
{
    return findResource(_passes, handle);
}

RGPassHandle RenderGraph::addPass(
    const std::string&                           name,
    const std::function<void(RGPassBuilder&)>&   setup,
    const std::function<void(RGRenderContext&)>& execute)
{
    RGPassHandle handle{
        .index      = static_cast<uint32_t>(_passes.size()),
        .generation = _nextPassGeneration++,
    };
    _passes.push_back(RGPass{
        .handle  = handle,
        .name    = name,
        .execute = execute,
    });

    RGPassBuilder builder(*this, _passes.size() - 1);
    setup(builder);
    return handle;
}

void RenderGraph::exportTexture(RGTextureHandle handle, std::string name)
{
    YA_CORE_ASSERT(handle.isValid(), "RenderGraph exportTexture requires a valid texture handle");
    YA_CORE_ASSERT(!name.empty(), "RenderGraph exportTexture requires a non-empty export name");
    _textureExports.push_back({
        .name    = std::move(name),
        .texture = handle,
    });
}

RGCompiledGraph RenderGraph::compile() const
{
    RGCompiledGraph compiled;
    compiled.order.reserve(_passes.size());
    compiled.passPlans.reserve(_passes.size());
    compiled.exportedTextures.reserve(_textureExports.size());
    compiled.importedTextureFinalizes.reserve(_textures.size());
    compiled.importedBufferFinalizes.reserve(_buffers.size());
    compiled.transientBufferLifetimes.reserve(_buffers.size());

    struct TrackedTextureAccess
    {
        std::optional<RGPassHandle> writer{};
        std::vector<RGPassHandle>   readers{};
    };
    std::unordered_map<RGTextureHandle, TrackedTextureAccess> textureAccesses;
    struct TrackedBufferAccess
    {
        RGPassHandle    pass{};
        RGBufferRange   range{};
        ERGBufferAccess access = ERGBufferAccess::StorageRead;
    };
    std::unordered_map<RGBufferHandle, std::vector<TrackedBufferAccess>> bufferAccesses;
    std::unordered_map<std::string, const RGTextureResource*>            persistentTexturesByKey;
    std::unordered_map<std::string, const RGBufferResource*>             persistentBuffersByKey;
    std::vector<std::vector<uint32_t>>                                   adjacency(_passes.size());
    std::vector<uint32_t>                                                indegree(_passes.size(), 0);
    std::vector<RGCompiledPassPlan>                                      passPlans(_passes.size());

    for (size_t i = 0; i < _passes.size(); ++i) {
        passPlans[i].pass       = _passes[i].handle;
        passPlans[i].rasterPlan = _passes[i].rasterDesc;
    }

    const auto addDependency = [&](RGPassHandle from, RGPassHandle to)
    {
        if (!from.isValid() || !to.isValid() || from == to) {
            return;
        }
        RGDependencyEdge edge{.from = from, .to = to};
        if (containsUsage(compiled.dependencies, edge)) {
            return;
        }
        compiled.dependencies.push_back(edge);
        adjacency[from.index].push_back(to.index);
        ++indegree[to.index];
    };

    const auto addIssue = [&](RGCompileIssue::EKind kind, RGPassHandle pass, std::string message)
    {
        compiled.issues.push_back({
            .kind    = kind,
            .pass    = pass,
            .message = std::move(message),
        });
    };

    const auto resolvePassKind = [](const RGPass& pass)
    {
        if (pass.kind != ERGPassKind::Unknown) {
            return pass.kind;
        }
        if (pass.rasterDesc.has_value()) {
            return ERGPassKind::Raster;
        }

        bool hasRasterUsage      = false;
        bool hasTransferUsage    = false;
        bool hasNonTransferUsage = false;

        for (const auto& usage : pass.textures) {
            if (usage.access == ERGPassResourceAccess::ColorAttachment ||
                usage.access == ERGPassResourceAccess::DepthAttachment) {
                hasRasterUsage = true;
            }
            else if (isTransferTextureAccess(usage.access)) {
                hasTransferUsage = true;
            }
            else {
                hasNonTransferUsage = true;
            }
        }

        for (const auto& usage : pass.buffers) {
            if (isTransferBufferAccess(usage.access)) {
                hasTransferUsage = true;
            }
            else {
                hasNonTransferUsage = true;
            }
        }

        if (hasRasterUsage) {
            return ERGPassKind::Raster;
        }
        if (hasTransferUsage && !hasNonTransferUsage) {
            return ERGPassKind::Copy;
        }
        return ERGPassKind::Compute;
    };

    const auto validatePassKind = [&](const RGPass& pass, ERGPassKind kind)
    {
        bool hasRasterUsage   = false;
        bool hasTransferUsage = false;
        bool hasOtherUsage    = false;

        for (const auto& usage : pass.textures) {
            if (usage.access == ERGPassResourceAccess::ColorAttachment ||
                usage.access == ERGPassResourceAccess::DepthAttachment) {
                hasRasterUsage = true;
            }
            else if (isTransferTextureAccess(usage.access)) {
                hasTransferUsage = true;
            }
            else {
                hasOtherUsage = true;
            }
        }
        for (const auto& usage : pass.buffers) {
            if (isTransferBufferAccess(usage.access)) {
                hasTransferUsage = true;
            }
            else {
                hasOtherUsage = true;
            }
        }

        switch (kind) {
        case ERGPassKind::Raster:
            if (!pass.rasterDesc.has_value() && !hasRasterUsage) {
                addIssue(RGCompileIssue::EKind::InvalidPassKind,
                         pass.handle,
                         std::format("pass {} is Raster but has no raster declaration or raster attachment usage", pass.name));
            }
            break;
        case ERGPassKind::Compute:
            if (pass.rasterDesc.has_value() || hasRasterUsage) {
                addIssue(RGCompileIssue::EKind::InvalidPassKind,
                         pass.handle,
                         std::format("pass {} is Compute but declares raster attachments", pass.name));
            }
            break;
        case ERGPassKind::Copy:
            if (pass.rasterDesc.has_value() || hasRasterUsage || hasOtherUsage) {
                addIssue(RGCompileIssue::EKind::InvalidPassKind,
                         pass.handle,
                         std::format("pass {} is Copy but uses non-transfer resources", pass.name));
            }
            if (!hasTransferUsage) {
                addIssue(RGCompileIssue::EKind::InvalidPassKind,
                         pass.handle,
                         std::format("pass {} is Copy but has no transfer resource usage", pass.name));
            }
            break;
        case ERGPassKind::Unknown:
            break;
        }
    };

    for (const RGPass& pass : _passes) {
        passPlans[pass.handle.index].kind = resolvePassKind(pass);
        validatePassKind(pass, passPlans[pass.handle.index].kind);

        for (const RGHandle<RGPassHandleTag> dependency : pass.dependencies) {
            if (!getPass(dependency)) {
                addIssue(RGCompileIssue::EKind::InvalidResource, pass.handle, std::format("pass {} depends on invalid pass handle", pass.name));
                continue;
            }
            addDependency(dependency, pass.handle);
        }

        for (const auto& usage : pass.textures) {
            const auto* resource = getTexture(usage.handle);
            if (!resource) {
                addIssue(RGCompileIssue::EKind::InvalidResource, pass.handle, std::format("pass {} references invalid texture handle", pass.name));
                continue;
            }

            const auto requiredUsage = [&]()
            {
                switch (usage.access) {
                case ERGPassResourceAccess::Read:
                    return EImageUsage::Sampled;
                case ERGPassResourceAccess::Write:
                    return EImageUsage::Storage;
                case ERGPassResourceAccess::ColorAttachment:
                    return EImageUsage::ColorAttachment;
                case ERGPassResourceAccess::DepthAttachment:
                    return EImageUsage::DepthStencilAttachment;
                case ERGPassResourceAccess::TransferSrc:
                    return EImageUsage::TransferSrc;
                case ERGPassResourceAccess::TransferDst:
                    return EImageUsage::TransferDst;
                }
                return EImageUsage::None;
            }();
            if (requiredUsage != EImageUsage::None && !hasImageUsage(resource->desc.usage, requiredUsage)) {
                const std::string backingUsage = resource->imported.has_value()
                                                   ? formatImageUsageFlags(resource->imported->importDesc.usage)
                                                   : formatImageUsageFlags(resource->desc.usage);
                addIssue(RGCompileIssue::EKind::InvalidUsage, pass.handle,
                         std::format("pass {} uses texture {} as {} but graph usage is {} (backing usage: {})",
                                     pass.name,
                                     resource->desc.label,
                                     describeRequiredImageUsage(usage.access),
                                     formatImageUsageFlags(resource->desc.usage),
                                     backingUsage));
                continue;
            }

            passPlans[pass.handle.index].textureStates.push_back({
                .pass          = pass.handle,
                .texture       = usage.handle,
                .requiredState = makeTextureState(*resource, usage.access),
            });

            const bool bWrite =
                usage.access == ERGPassResourceAccess::Write ||
                usage.access == ERGPassResourceAccess::ColorAttachment ||
                usage.access == ERGPassResourceAccess::DepthAttachment ||
                usage.access == ERGPassResourceAccess::TransferDst;

            auto& textureAccess = textureAccesses[usage.handle];
            if (!bWrite) {
                if (!textureAccess.writer.has_value() && resource->lifetime != ERGResourceLifetime::Imported) {
                    addIssue(RGCompileIssue::EKind::ReadBeforeWrite, pass.handle, std::format("pass {} reads texture {} before any writer", pass.name, resource->desc.label));
                }
                else if (textureAccess.writer.has_value()) {
                    addDependency(*textureAccess.writer, pass.handle);
                }
                textureAccess.readers.push_back(pass.handle);
                continue;
            }

            if (textureAccess.writer.has_value()) {
                addDependency(*textureAccess.writer, pass.handle);
            }
            for (const auto reader : textureAccess.readers) {
                addDependency(reader, pass.handle);
            }
            textureAccess.readers.clear();
            textureAccess.writer = pass.handle;
        }

        for (const auto& usage : pass.buffers) {
            const auto* resource = getBuffer(usage.handle);
            if (!resource) {
                addIssue(RGCompileIssue::EKind::InvalidResource, pass.handle, std::format("pass {} references invalid buffer handle", pass.name));
                continue;
            }

            const bool bCompatibleUsage = validateBufferUsageFlags(*resource, usage.access);
            const bool bBackingUsageCompatible =
                !resource->imported.has_value() ||
                validateBufferUsageFlags(RGBufferResource{
                                             .desc = RGBufferDesc{
                                                 .label = resource->desc.label,
                                                 .usage = resource->imported->buffer ? resource->imported->buffer->getUsage() : resource->desc.usage,
                                                 .size  = resource->desc.size,
                                             },
                                         },
                                         usage.access);
            if (!bCompatibleUsage) {
                addIssue(RGCompileIssue::EKind::InvalidUsage, pass.handle, std::format("pass {} uses buffer {} as {} but graph usage is {}{}", pass.name, resource->desc.label, describeRequiredBufferUsage(usage.access), formatBufferUsageFlags(resource->desc.usage), resource->imported.has_value() && resource->imported->buffer ? std::format(" (backing usage: {})", formatBufferUsageFlags(resource->imported->buffer->getUsage())) : ""));
                continue;
            }
            if (!bBackingUsageCompatible) {
                addIssue(RGCompileIssue::EKind::InvalidUsage, pass.handle, std::format("pass {} uses imported buffer {} as {} and graph usage is {}, but backing buffer usage is {}", pass.name, resource->desc.label, describeRequiredBufferUsage(usage.access), formatBufferUsageFlags(resource->desc.usage), resource->imported && resource->imported->buffer ? formatBufferUsageFlags(resource->imported->buffer->getUsage()) : std::string("None")));
                continue;
            }

            const auto normalizedRange = normalizeBufferRange(*resource, usage.range);

            passPlans[pass.handle.index].bufferStates.push_back({
                .pass          = pass.handle,
                .buffer        = usage.handle,
                .requiredState = makeBufferState(passPlans[pass.handle.index].kind, *resource, usage),
            });

            const bool bRead         = isBufferReadAccess(usage.access);
            const bool bWrite        = isBufferWriteAccess(usage.access);
            auto&      priorAccesses = bufferAccesses[usage.handle];

            bool bHasOverlappingWriter = false;
            for (const auto& prior : priorAccesses) {
                if (!rangesOverlap(prior.range, normalizedRange)) {
                    continue;
                }
                if ((bRead && isBufferWriteAccess(prior.access)) ||
                    (bWrite && (isBufferReadAccess(prior.access) || isBufferWriteAccess(prior.access)))) {
                    addDependency(prior.pass, pass.handle);
                }
                if (bRead && isBufferWriteAccess(prior.access)) {
                    bHasOverlappingWriter = true;
                }
            }

            if (bRead && !bWrite && !bHasOverlappingWriter && resource->lifetime != ERGResourceLifetime::Imported) {
                addIssue(RGCompileIssue::EKind::ReadBeforeWrite,
                         pass.handle,
                         std::format("pass {} reads buffer {} range {} before any overlapping writer",
                                     pass.name,
                                     resource->desc.label,
                                     formatBufferRange(normalizedRange)));
            }

            priorAccesses.push_back({
                .pass   = pass.handle,
                .range  = normalizedRange,
                .access = usage.access,
            });
        }
    }

    for (const auto& texture : _textures) {
        if (texture.lifetime != ERGResourceLifetime::Persistent) {
            continue;
        }
        if (!texture.persistentKey.has_value() || !texture.persistentKey->isValid()) {
            addIssue(RGCompileIssue::EKind::InvalidPersistentIdentity,
                     RGPassHandle{},
                     std::format("persistent texture {} is missing a stable key", texture.desc.label));
            continue;
        }

        if (const auto existing = persistentTexturesByKey.find(texture.persistentKey->value);
            existing != persistentTexturesByKey.end()) {
            if (existing->second->desc.format != texture.desc.format ||
                existing->second->desc.extent.width != texture.desc.extent.width ||
                existing->second->desc.extent.height != texture.desc.extent.height ||
                existing->second->desc.extent.depth != texture.desc.extent.depth ||
                existing->second->desc.mipLevels != texture.desc.mipLevels ||
                existing->second->desc.arrayLayers != texture.desc.arrayLayers ||
                existing->second->desc.samples != texture.desc.samples ||
                existing->second->desc.usage != texture.desc.usage ||
                existing->second->desc.flags != texture.desc.flags) {
                addIssue(RGCompileIssue::EKind::InvalidPersistentIdentity,
                         RGPassHandle{},
                         std::format("persistent texture key '{}' maps to conflicting descriptors ('{}' vs '{}')",
                                     texture.persistentKey->value,
                                     existing->second->desc.label,
                                     texture.desc.label));
            }
            continue;
        }
        persistentTexturesByKey.emplace(texture.persistentKey->value, &texture);
    }

    for (const auto& buffer : _buffers) {
        if (buffer.lifetime != ERGResourceLifetime::Persistent) {
            continue;
        }
        if (!buffer.persistentKey.has_value() || !buffer.persistentKey->isValid()) {
            addIssue(RGCompileIssue::EKind::InvalidPersistentIdentity,
                     RGPassHandle{},
                     std::format("persistent buffer {} is missing a stable key", buffer.desc.label));
            continue;
        }

        if (const auto existing = persistentBuffersByKey.find(buffer.persistentKey->value);
            existing != persistentBuffersByKey.end()) {
            if (existing->second->desc.usage != buffer.desc.usage ||
                existing->second->desc.size != buffer.desc.size ||
                existing->second->desc.memoryUsage != buffer.desc.memoryUsage ||
                existing->second->desc.alignment != buffer.desc.alignment) {
                addIssue(RGCompileIssue::EKind::InvalidPersistentIdentity,
                         RGPassHandle{},
                         std::format("persistent buffer key '{}' maps to conflicting descriptors ('{}' vs '{}')",
                                     buffer.persistentKey->value,
                                     existing->second->desc.label,
                                     buffer.desc.label));
            }
            continue;
        }
        persistentBuffersByKey.emplace(buffer.persistentKey->value, &buffer);
    }

    std::unordered_set<std::string> exportedTextureNames;
    exportedTextureNames.reserve(_textureExports.size());
    for (const auto& exported : _textureExports) {
        const auto* resource = getTexture(exported.texture);
        if (!resource) {
            addIssue(RGCompileIssue::EKind::InvalidResource,
                     RGPassHandle{},
                     std::format("exported texture {} references invalid handle {}", exported.name, exported.texture.index));
            continue;
        }
        if (!exportedTextureNames.insert(exported.name).second) {
            addIssue(RGCompileIssue::EKind::InvalidResource,
                     RGPassHandle{},
                     std::format("duplicate exported texture name {}", exported.name));
            continue;
        }
        compiled.exportedTextures.push_back({
            .name    = exported.name,
            .texture = exported.texture,
        });
    }

    std::deque<uint32_t> ready;
    for (uint32_t i = 0; i < indegree.size(); ++i) {
        if (indegree[i] == 0) {
            ready.push_back(i);
        }
    }

    while (!ready.empty()) {
        const auto index = ready.front();
        ready.pop_front();
        compiled.order.push_back(_passes[index].handle);

        auto neighbors = adjacency[index];
        std::sort(neighbors.begin(), neighbors.end());
        for (const auto next : neighbors) {
            if (--indegree[next] == 0) {
                ready.push_back(next);
            }
        }
    }

    if (compiled.order.size() != _passes.size()) {
        compiled.order.clear();
        addIssue(RGCompileIssue::EKind::Cycle, RGPassHandle{}, "render graph contains a dependency cycle");
    }
    else {
        for (const auto& passHandle : compiled.order) {
            compiled.passPlans.push_back(std::move(passPlans[passHandle.index]));
        }
    }

    for (const auto& texture : _textures) {
        if (texture.lifetime != ERGResourceLifetime::Imported || !texture.imported.has_value()) {
            continue;
        }

        const auto finalLayout = texture.imported->importDesc.finalLayout;
        if (finalLayout == EImageLayout::Undefined) {
            continue;
        }

        compiled.importedTextureFinalizes.push_back({
            .texture          = texture.handle,
            .finalLayout      = finalLayout,
            .subresourceRange = texture.imported->subresourceRange,
        });
    }

    for (const auto& buffer : _buffers) {
        if (buffer.lifetime != ERGResourceLifetime::Imported || !buffer.imported.has_value() ||
            !buffer.imported->finalState.has_value()) {
            continue;
        }

        compiled.importedBufferFinalizes.push_back({
            .buffer       = buffer.handle,
            .initialState = buffer.imported->initialState,
            .finalState   = *buffer.imported->finalState,
        });
    }

    if (compiled.isValid()) {
        std::unordered_map<RGBufferHandle, RGTransientBufferLifetimePlan*> transientLifetimeByHandle;
        for (const auto& buffer : _buffers) {
            if (buffer.lifetime != ERGResourceLifetime::Transient) {
                continue;
            }
            compiled.transientBufferLifetimes.push_back({
                .buffer = buffer.handle,
                .desc   = buffer.desc,
            });
            transientLifetimeByHandle.emplace(buffer.handle, &compiled.transientBufferLifetimes.back());
            ++compiled.transientBufferDiagnostics.logicalCount;
            compiled.transientBufferDiagnostics.logicalBytes += buffer.desc.size;
        }

        for (uint32_t passIndex = 0; passIndex < compiled.passPlans.size(); ++passIndex) {
            const auto& passPlan = compiled.passPlans[passIndex];
            for (const auto& bufferState : passPlan.bufferStates) {
                const auto lifetimeIt = transientLifetimeByHandle.find(bufferState.buffer);
                if (lifetimeIt == transientLifetimeByHandle.end()) {
                    continue;
                }
                auto& lifetime = *lifetimeIt->second;
                if (!lifetime.isUsed()) {
                    lifetime.firstPassIndex = passIndex;
                    lifetime.firstPass      = passPlan.pass;
                }
                lifetime.lastPassIndex = passIndex;
                lifetime.lastPass      = passPlan.pass;
            }
        }

        std::sort(compiled.transientBufferLifetimes.begin(),
                  compiled.transientBufferLifetimes.end(),
                  [](const RGTransientBufferLifetimePlan& lhs, const RGTransientBufferLifetimePlan& rhs)
                  {
                      const bool lhsUsed = lhs.isUsed();
                      const bool rhsUsed = rhs.isUsed();
                      if (lhsUsed != rhsUsed) {
                          return lhsUsed && !rhsUsed;
                      }
                      if (lhsUsed && rhsUsed) {
                          if (lhs.firstPassIndex != rhs.firstPassIndex) {
                              return lhs.firstPassIndex < rhs.firstPassIndex;
                          }
                          if (lhs.lastPassIndex != rhs.lastPassIndex) {
                              return lhs.lastPassIndex < rhs.lastPassIndex;
                          }
                      }
                      return isHandleDeterministicallyBefore(lhs.buffer, rhs.buffer);
                  });

        for (const auto& lifetime : compiled.transientBufferLifetimes) {
            if (lifetime.isUsed()) {
                ++compiled.transientBufferDiagnostics.usedCount;
                compiled.transientBufferDiagnostics.usedBytes += lifetime.desc.size;
            }
            else {
                ++compiled.transientBufferDiagnostics.unusedCount;
                compiled.transientBufferDiagnostics.unusedBytes += lifetime.desc.size;
            }
        }

        const auto findLifetime = [&](RGBufferHandle handle) -> const RGTransientBufferLifetimePlan*
        {
            const auto lifetimeIt = std::find_if(
                compiled.transientBufferLifetimes.begin(),
                compiled.transientBufferLifetimes.end(),
                [handle](const RGTransientBufferLifetimePlan& candidate)
                {
                    return candidate.buffer == handle;
                });
            return lifetimeIt != compiled.transientBufferLifetimes.end() ? &*lifetimeIt : nullptr;
        };

        compiled.transientBufferAssignments.reserve(compiled.transientBufferDiagnostics.usedCount);
        for (const auto& lifetime : compiled.transientBufferLifetimes) {
            if (!lifetime.isUsed()) {
                continue;
            }

            auto slotIt = std::find_if(
                compiled.transientBufferSlots.begin(),
                compiled.transientBufferSlots.end(),
                [&](const RGTransientBufferSlotPlan& slot)
                {
                    if (slot.desc.memoryUsage != lifetime.desc.memoryUsage) {
                        return false;
                    }
                    return std::none_of(slot.buffers.begin(), slot.buffers.end(), [&](RGBufferHandle member)
                                        {
                        const auto* memberLifetime = findLifetime(member);
                        return memberLifetime != nullptr && transientLifetimesOverlap(*memberLifetime, lifetime); });
                });

            if (slotIt == compiled.transientBufferSlots.end()) {
                const auto                slotIndex = static_cast<uint32_t>(compiled.transientBufferSlots.size());
                RGTransientBufferSlotPlan slot{
                    .slotIndex = slotIndex,
                    .desc      = lifetime.desc,
                    .buffers   = {lifetime.buffer},
                };
                slot.desc.label = std::format("transient.slot.{}", slotIndex);
                compiled.transientBufferSlots.push_back(std::move(slot));
                slotIt = std::prev(compiled.transientBufferSlots.end());
            }
            else {
                slotIt->desc.size      = std::max(slotIt->desc.size, lifetime.desc.size);
                slotIt->desc.usage     = slotIt->desc.usage | lifetime.desc.usage;
                slotIt->desc.alignment = std::max(slotIt->desc.alignment, lifetime.desc.alignment);
                slotIt->buffers.push_back(lifetime.buffer);
            }

            compiled.transientBufferAssignments.push_back({
                .buffer    = lifetime.buffer,
                .slotIndex = slotIt->slotIndex,
            });
        }

        compiled.transientBufferDiagnostics.physicalSlotCount =
            static_cast<uint32_t>(compiled.transientBufferSlots.size());
        for (const auto& slot : compiled.transientBufferSlots) {
            compiled.transientBufferDiagnostics.physicalBytes += slot.desc.size;
        }
        compiled.transientBufferDiagnostics.aliasedBufferCount =
            compiled.transientBufferDiagnostics.usedCount - compiled.transientBufferDiagnostics.physicalSlotCount;
        if (compiled.transientBufferDiagnostics.usedCount > 0) {
            compiled.transientBufferDiagnostics.reuseRatio =
                static_cast<double>(compiled.transientBufferDiagnostics.aliasedBufferCount) /
                static_cast<double>(compiled.transientBufferDiagnostics.usedCount);
        }

        for (const auto& slot : compiled.transientBufferSlots) {
            for (size_t memberIndex = 1; memberIndex < slot.buffers.size(); ++memberIndex) {
                const auto* previousLifetime = findLifetime(slot.buffers[memberIndex - 1]);
                const auto* nextLifetime     = findLifetime(slot.buffers[memberIndex]);
                YA_CORE_ASSERT(previousLifetime != nullptr && nextLifetime != nullptr,
                               "RenderGraph transient slot {} references a missing lifetime",
                               slot.slotIndex);
                YA_CORE_ASSERT(previousLifetime->lastPassIndex < nextLifetime->firstPassIndex,
                               "RenderGraph transient slot {} contains overlapping lifetimes",
                               slot.slotIndex);
                compiled.transientBufferAliasBoundaries.push_back({
                    .slotIndex      = slot.slotIndex,
                    .previousBuffer = previousLifetime->buffer,
                    .nextBuffer     = nextLifetime->buffer,
                    .nextPass       = nextLifetime->firstPass,
                });
            }
        }
        compiled.transientBufferDiagnostics.aliasBoundaryCount =
            static_cast<uint32_t>(compiled.transientBufferAliasBoundaries.size());
    }

    return compiled;
}

std::optional<RGPassContext> RenderGraph::createPassContext(RGPassHandle handle) const
{
    const auto* pass = getPass(handle);
    if (!pass) {
        return std::nullopt;
    }
    return RGPassContext(*this, *pass);
}

RGTopologyDescription RenderGraph::describeCompiledTopology(const RGCompiledGraph& compiled) const
{
    RGTopologyDescription topology{};
    topology.passOrder.reserve(compiled.passPlans.size());
    topology.dependencies.reserve(compiled.dependencies.size());

    for (uint32_t orderIndex = 0; orderIndex < compiled.passPlans.size(); ++orderIndex) {
        const auto& passPlan = compiled.passPlans[orderIndex];
        const auto* pass     = getPass(passPlan.pass);
        topology.passOrder.push_back({
            .pass       = passPlan.pass,
            .name       = pass ? std::string_view(pass->name) : std::string_view{},
            .kind       = passPlan.kind,
            .orderIndex = orderIndex,
        });
    }

    for (const auto& edge : compiled.dependencies) {
        const auto* from = getPass(edge.from);
        const auto* to   = getPass(edge.to);
        topology.dependencies.push_back({
            .from     = edge.from,
            .to       = edge.to,
            .fromName = from ? std::string_view(from->name) : std::string_view{},
            .toName   = to ? std::string_view(to->name) : std::string_view{},
        });
    }

    return topology;
}

std::string RenderGraph::debugDump(const RGCompiledGraph& compiled) const
{
    std::ostringstream oss;
    const auto         topology = describeCompiledTopology(compiled);
    oss << "passes(" << _passes.size() << ")\n";
    for (const auto& pass : _passes) {
        oss << "  [" << pass.handle.index << ":" << pass.handle.generation << "] " << pass.name << "\n";
    }

    oss << "order(" << topology.passOrder.size() << ")\n";
    for (const auto& passInfo : topology.passOrder) {
        oss << "  [" << passInfo.pass.index << ":" << passInfo.pass.generation << "] "
            << (!passInfo.name.empty() ? passInfo.name : std::string_view{"<invalid-pass>"}) << "\n";
    }

    oss << "dependencies(" << topology.dependencies.size() << ")\n";
    for (const auto& edge : topology.dependencies) {
        oss << "  " << (!edge.fromName.empty() ? edge.fromName : std::string_view{"<invalid-pass>"})
            << " -> " << (!edge.toName.empty() ? edge.toName : std::string_view{"<invalid-pass>"}) << "\n";
    }

    oss << "passPlans(" << compiled.passPlans.size() << ")\n";
    for (const auto& passPlan : compiled.passPlans) {
        const auto* pass = getPass(passPlan.pass);
        oss << "  [" << passPlan.pass.index << ":" << passPlan.pass.generation << "] "
            << (pass ? pass->name : "<invalid-pass>")
            << " kind=" << toString(passPlan.kind) << "\n";

        if (passPlan.rasterPlan.has_value()) {
            oss << "    raster colors=" << passPlan.rasterPlan->colors.size()
                << " depth=" << (passPlan.rasterPlan->depth.has_value() ? 1 : 0)
                << " layers=" << passPlan.rasterPlan->layerCount << "\n";
        }

        oss << "    textureStates(" << passPlan.textureStates.size() << ")\n";
        for (const auto& state : passPlan.textureStates) {
            const auto* texture = getTexture(state.texture);
            oss << "      " << (texture ? texture->desc.label : "<invalid-texture>")
                << " layout=" << static_cast<uint32_t>(state.requiredState.layout) << "\n";
        }

        oss << "    bufferStates(" << passPlan.bufferStates.size() << ")\n";
        for (const auto& state : passPlan.bufferStates) {
            const auto* buffer = getBuffer(state.buffer);
            oss << "      " << (buffer ? buffer->desc.label : "<invalid-buffer>")
                << " access=" << static_cast<uint32_t>(state.requiredState.access) << "\n";
        }
    }

    oss << "importedTextureFinalizes(" << compiled.importedTextureFinalizes.size() << ")\n";
    for (const auto& finalize : compiled.importedTextureFinalizes) {
        const auto* texture = getTexture(finalize.texture);
        oss << "  " << (texture ? texture->desc.label : "<invalid-texture>")
            << " layout=" << static_cast<uint32_t>(finalize.finalLayout) << "\n";
    }

    oss << "importedBufferFinalizes(" << compiled.importedBufferFinalizes.size() << ")\n";
    for (const auto& finalize : compiled.importedBufferFinalizes) {
        const auto* buffer = getBuffer(finalize.buffer);
        oss << "  " << (buffer ? buffer->desc.label : "<invalid-buffer>")
            << " access=" << static_cast<uint32_t>(finalize.finalState.access) << "\n";
    }

    oss << "exportedTextures(" << compiled.exportedTextures.size() << ")\n";
    for (const auto& exported : compiled.exportedTextures) {
        const auto* texture = getTexture(exported.texture);
        oss << "  " << exported.name << " -> "
            << (texture ? texture->desc.label : "<invalid-texture>") << "\n";
    }

    oss << "transientBufferLifetimes(" << compiled.transientBufferLifetimes.size() << ")\n";
    for (const auto& lifetime : compiled.transientBufferLifetimes) {
        const auto* buffer = getBuffer(lifetime.buffer);
        oss << "  " << (buffer ? buffer->desc.label : "<invalid-buffer>");
        if (lifetime.isUsed()) {
            const auto* firstPass = getPass(lifetime.firstPass);
            const auto* lastPass  = getPass(lifetime.lastPass);
            oss << " first=" << lifetime.firstPassIndex
                << ":" << (firstPass ? firstPass->name : "<invalid-pass>")
                << " last=" << lifetime.lastPassIndex
                << ":" << (lastPass ? lastPass->name : "<invalid-pass>");
        }
        else {
            oss << " unused";
        }
        oss << "\n";
    }

    oss << "transientBufferAssignments(" << compiled.transientBufferAssignments.size() << ")\n";
    for (const auto& assignment : compiled.transientBufferAssignments) {
        const auto* buffer = getBuffer(assignment.buffer);
        oss << "  " << (buffer ? buffer->desc.label : "<invalid-buffer>")
            << " -> slot=" << assignment.slotIndex << "\n";
    }

    oss << "transientBufferSlots(" << compiled.transientBufferSlots.size() << ")\n";
    for (const auto& slot : compiled.transientBufferSlots) {
        oss << "  slot=" << slot.slotIndex
            << " label=" << slot.desc.label
            << " size=" << slot.desc.size
            << " alignment=" << slot.desc.alignment
            << " usage=" << static_cast<uint32_t>(slot.desc.usage)
            << " members=" << slot.buffers.size() << "\n";
    }

    oss << "transientBufferAliasBoundaries(" << compiled.transientBufferAliasBoundaries.size() << ")\n";
    for (const auto& boundary : compiled.transientBufferAliasBoundaries) {
        const auto* previousBuffer = getBuffer(boundary.previousBuffer);
        const auto* nextBuffer     = getBuffer(boundary.nextBuffer);
        const auto* nextPass       = getPass(boundary.nextPass);
        oss << "  slot=" << boundary.slotIndex
            << " " << (previousBuffer ? previousBuffer->desc.label : "<invalid-buffer>")
            << " -> " << (nextBuffer ? nextBuffer->desc.label : "<invalid-buffer>")
            << " at " << (nextPass ? nextPass->name : "<invalid-pass>") << "\n";
    }

    const auto& transientDiagnostics = compiled.transientBufferDiagnostics;
    oss << "transientBufferDiagnostics"
        << " logicalCount=" << transientDiagnostics.logicalCount
        << " logicalBytes=" << transientDiagnostics.logicalBytes
        << " usedCount=" << transientDiagnostics.usedCount
        << " usedBytes=" << transientDiagnostics.usedBytes
        << " unusedCount=" << transientDiagnostics.unusedCount
        << " unusedBytes=" << transientDiagnostics.unusedBytes
        << " physicalSlotCount=" << transientDiagnostics.physicalSlotCount
        << " physicalBytes=" << transientDiagnostics.physicalBytes
        << " aliasedBufferCount=" << transientDiagnostics.aliasedBufferCount
        << " aliasBoundaryCount=" << transientDiagnostics.aliasBoundaryCount
        << " reuseRatio=" << transientDiagnostics.reuseRatio
        << " physicalReuse=compiler-plan\n";

    oss << "issues(" << compiled.issues.size() << ")\n";
    for (const auto& issue : compiled.issues) {
        const auto* pass = getPass(issue.pass);
        oss << "  ";
        switch (issue.kind) {
        case RGCompileIssue::EKind::ReadBeforeWrite:
            oss << "ReadBeforeWrite";
            break;
        case RGCompileIssue::EKind::InvalidResource:
            oss << "InvalidResource";
            break;
        case RGCompileIssue::EKind::InvalidUsage:
            oss << "InvalidUsage";
            break;
        case RGCompileIssue::EKind::InvalidPassKind:
            oss << "InvalidPassKind";
            break;
        case RGCompileIssue::EKind::InvalidPersistentIdentity:
            oss << "InvalidPersistentIdentity";
            break;
        case RGCompileIssue::EKind::Cycle:
            oss << "Cycle";
            break;
        }
        if (pass) {
            oss << "@" << pass->name;
        }
        oss << ": " << issue.message << "\n";
    }

    return oss.str();
}

} // namespace ya
