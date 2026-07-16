#pragma once

#include "Render/RenderDefines.h"

#include <memory>
#include <optional>
#include <vector>

namespace ya
{

struct IImageView;
struct RenderImage;

struct RenderTargetEditorCatalog
{
    struct Entry
    {
        const char*    label = "";
        enum class EOwner
        {
            Presentation,
            ForwardViewport,
            ForwardShadow,
            DeferredGBuffer,
            DeferredViewport,
            DeferredShadow,
        } owner = EOwner::Presentation;
        std::vector<EFormat::T>          colorFormats{};
        std::optional<EFormat::T>        depthFormat{};
        std::vector<std::shared_ptr<RenderImage>> colorAttachments{};
        std::shared_ptr<RenderImage>              depthAttachment = nullptr;
        std::shared_ptr<IImageView>               depthAttachmentView = nullptr;
        Extent2D                                  extent{};
        uint32_t                                  frameBufferCount = 0;
        bool                                      bSwapChainTarget = false;
        bool bEditable = true;
    };

    std::vector<Entry> entries;
};

} // namespace ya
