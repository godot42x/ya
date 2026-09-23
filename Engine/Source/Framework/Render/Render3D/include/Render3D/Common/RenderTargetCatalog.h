#pragma once

#include "Render3D/Common/SceneViewDesc.h"
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
        /// The View this target belongs to, or 0 for a target no View owns (the
        /// window's present image, a shadow map). A row *is* one render target,
        /// so the identity is what tells two View targets of the same kind and
        /// size apart; the label only names the kind.
        SceneViewId    viewId = 0;
        enum class EOwner
        {
            Presentation,
            ForwardView,
            ForwardShadow,
            DeferredGBuffer,
            DeferredView,
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
