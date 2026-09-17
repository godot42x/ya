#pragma once

#include "RHI/RenderDefines.h"

#include <memory>
#include <optional>
#include <vector>

namespace ya
{

struct IImageView;
struct RenderTexture;

struct RenderTargetCatalog
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
        std::vector<std::shared_ptr<RenderTexture>> colorAttachments{};
        std::shared_ptr<RenderTexture>              depthAttachment = nullptr;
        std::shared_ptr<IImageView>               depthAttachmentView = nullptr;
        Extent2D                                  extent{};
        uint32_t                                  frameBufferCount = 0;
        bool                                      bSwapChainTarget = false;
        bool bEditable = true;
    };

    std::vector<Entry> entries;
};

struct RenderTargetFormatCommand
{
    enum class EAttachment
    {
        Color,
        Depth,
    } attachment = EAttachment::Color;

    RenderTargetCatalog::Entry::EOwner owner = RenderTargetCatalog::Entry::EOwner::Presentation;
    uint32_t                                 colorAttachmentIndex = 0;
    EFormat::T                               format = EFormat::Undefined;
};

} // namespace ya
