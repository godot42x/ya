#pragma once

#include "DeferredAttachmentFormats.h"

#include <memory>

namespace ya
{

struct RenderTexture;

struct DeferredViewportResources
{
    std::shared_ptr<RenderTexture> colorOwner = nullptr;
    std::shared_ptr<RenderTexture> depthOwner = nullptr;
    std::shared_ptr<RenderTexture> entityIdOwner = nullptr;
    RenderTexture*                 color      = nullptr;
    RenderTexture*                 depth      = nullptr;
    RenderTexture*                 entityId   = nullptr;
    DeferredAttachmentFormats formats{};

    [[nodiscard]] bool isComplete() const
    {
        return color != nullptr && depth != nullptr;
    }

    void reset(const DeferredAttachmentFormats& nextFormats = {})
    {
        colorOwner.reset();
        depthOwner.reset();
        entityIdOwner.reset();
        color   = nullptr;
        depth   = nullptr;
        entityId = nullptr;
        formats = nextFormats;
    }

    void publish(std::shared_ptr<RenderTexture> nextColorOwner,
                 std::shared_ptr<RenderTexture> nextDepthOwner,
                 std::shared_ptr<RenderTexture> nextEntityIdOwner,
                 const DeferredAttachmentFormats& nextFormats)
    {
        colorOwner = std::move(nextColorOwner);
        depthOwner = std::move(nextDepthOwner);
        entityIdOwner = std::move(nextEntityIdOwner);
        color      = colorOwner.get();
        depth      = depthOwner.get();
        entityId   = entityIdOwner.get();
        formats    = nextFormats;
    }
};

} // namespace ya
