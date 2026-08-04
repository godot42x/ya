#include "Render/Core/RenderGraph.h"
#include "Render/Core/RenderGraphResourceRegistry.h"

#include <algorithm>

namespace ya
{

namespace
{

bool isReadableBufferAccess(ERGBufferAccess access)
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

} // namespace

const RGTextureUsage* RGRenderContext::RGPassBindingContext::findDeclaredTextureUsage(RGTextureHandle handle) const
{
    const auto it = std::find_if(_pass.textures.begin(),
                                 _pass.textures.end(),
                                 [handle](const RGTextureUsage& usage)
                                 { return usage.handle == handle; });
    return it != _pass.textures.end() ? &*it : nullptr;
}

const RGBufferUsage* RGRenderContext::RGPassBindingContext::findDeclaredBufferUsage(RGBufferHandle handle) const
{
    const auto it = std::find_if(_pass.buffers.begin(),
                                 _pass.buffers.end(),
                                 [handle](const RGBufferUsage& usage)
                                 { return usage.handle == handle; });
    return it != _pass.buffers.end() ? &*it : nullptr;
}

std::optional<DescriptorImageInfo> RGRenderContext::RGPassBindingContext::resolveTextureDescriptor(
    RGTextureHandle handle,
    Sampler*        sampler) const
{
    if (!handle.isValid() || !sampler) {
        return std::nullopt;
    }

    const auto* usage = findDeclaredTextureUsage(handle);
    YA_CORE_ASSERT(usage != nullptr,
                   "RGPassBindingContext pass {} attempted texture descriptor for undeclared handle {}",
                   _pass.name,
                   handle.index);
    YA_CORE_ASSERT(usage->access == ERGPassResourceAccess::Read,
                   "RGPassBindingContext pass {} attempted sampled descriptor for texture handle {} declared as {}",
                   _pass.name,
                   handle.index,
                   static_cast<int>(usage->access));

    const auto* texture = _registry.resolveTexture(handle);
    if (!texture || !texture->getImageView()) {
        return std::nullopt;
    }

    // Keep the resolved image/view and imported owners alive until the
    // command buffer completes.
    _cmdBuf.retainResource(texture->getImageShared());
    _cmdBuf.retainResource(texture->getImageViewShared());
    _cmdBuf.retainResources(texture->getRetainedResources());
    return DescriptorImageInfo{
        .imageView   = texture->getImageView()->getHandle(),
        .sampler     = sampler->getHandle(),
        .imageLayout = EImageLayout::ShaderReadOnlyOptimal,
    };
}

std::optional<DescriptorBufferInfo> RGRenderContext::RGPassBindingContext::resolveBufferDescriptor(
    RGBufferHandle handle) const
{
    if (!handle.isValid()) {
        return std::nullopt;
    }

    const auto* usage = findDeclaredBufferUsage(handle);
    YA_CORE_ASSERT(usage != nullptr,
                   "RGPassBindingContext pass {} attempted buffer descriptor for undeclared handle {}",
                   _pass.name,
                   handle.index);
    YA_CORE_ASSERT(isReadableBufferAccess(usage->access),
                   "RGPassBindingContext pass {} attempted descriptor for buffer handle {} declared as write-only",
                   _pass.name,
                   handle.index);

    auto* buffer = _registry.resolveBuffer(handle);
    if (!buffer) {
        return std::nullopt;
    }

    // Imported buffer keep-alive owners are already retained by the executor
    // while applying this pass's buffer state plan; the descriptor path only
    // needs to resolve the declared range.

    uint64_t offset = usage->range.offset;
    uint64_t size   = usage->range.size;
    if (size == 0) {
        YA_CORE_ASSERT(offset <= buffer->getSize(),
                       "RGPassBindingContext pass {} buffer handle {} range offset {} exceeds size {}",
                       _pass.name,
                       handle.index,
                       offset,
                       buffer->getSize());
        size = buffer->getSize() - offset;
    }

    return DescriptorBufferInfo(buffer->getHandle(), offset, size);
}

} // namespace ya
