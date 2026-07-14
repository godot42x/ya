#pragma once

#include "DeferredAttachmentFormats.h"

#include <array>
#include <memory>

namespace ya
{

struct RenderImage;

struct DeferredGBufferResources
{
    std::array<std::shared_ptr<RenderImage>, 4> colorOwners{};
    std::shared_ptr<RenderImage>                depthOwner = nullptr;
    std::array<RenderImage*, 4> color{};
    RenderImage*                depth = nullptr;
    DeferredAttachmentFormats   formats{};

    [[nodiscard]] bool isComplete() const
    {
        return color[0] != nullptr &&
               color[1] != nullptr &&
               color[2] != nullptr &&
               color[3] != nullptr &&
               depth != nullptr;
    }

    void syncRawViews()
    {
        for (uint32_t attachmentIndex = 0; attachmentIndex < color.size(); ++attachmentIndex) {
            color[attachmentIndex] = colorOwners[attachmentIndex].get();
        }
        depth = depthOwner.get();
    }
};

} // namespace ya
