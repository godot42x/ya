#pragma once

#include "Render/Core/Buffer.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"

namespace ya
{

/// Allocates a per-flight point shadow backing buffer. Point shadow passes
/// keep these as capacity-managed cross-frame owners; they are never RDG
/// transient resources.
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
