#pragma once

#include "RHI/Core/Buffer.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"

namespace ya
{

/// Allocates a View-owned point shadow backing buffer. Capacity lives on
/// `PointShadowIndirectResources`; these are never RDG transient resources.
inline stdptr<IBuffer> createPointShadowBuffer(IRender* render,
                                               std::string label,
                                               EBufferUsage usage,
                                               uint32_t size,
                                               EMemoryUsage memoryUsage)
{
    return render->getResourceFactory()->createBuffer(BufferCreateInfo{
        .label       = std::move(label),
        .usage       = usage,
        .size        = size,
        .memoryUsage = memoryUsage,
    });
}

} // namespace ya
