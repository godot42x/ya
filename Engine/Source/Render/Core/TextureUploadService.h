#pragma once

#include "Render/RenderDefines.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ya
{

struct IBuffer;
struct IImage;
struct IRender;
struct BufferImageCopy;
struct ImageSubresourceRange;

/// Upload request: a staging buffer's copy regions into an existing image.
///
/// The image must already exist with TransferDst usage; the service records
/// Undefined -> TransferDst -> finalLayout transitions plus all copy regions
/// inside one isolate-command scope (FG-803). Texture no longer drives
/// begin/end isolate commands itself.
struct TextureUploadRequest
{
    std::shared_ptr<IImage>          image{};
    std::shared_ptr<IBuffer>         staging{};
    std::vector<BufferImageCopy>     regions{};
    /// Optional subresource range for the initial Undefined -> TransferDst
    /// transition; absent means the full image.
    std::optional<ImageSubresourceRange> uploadRange{};
    /// Optional subresource range for the final TransferDst -> finalLayout
    /// transition; absent means the full image.
    std::optional<ImageSubresourceRange> finalizeRange{};
    bool                           bGenerateMipmaps = false;
    EImageLayout::T                finalLayout = EImageLayout::ShaderReadOnlyOptimal;
    std::string                    label;
};

/// Owns texture upload command recording and submission.
///
/// Explicitly depends on IRender for the resource factory and isolate-command
/// submission; it never reaches for a global render or App.
struct TextureUploadService
{
    /// Records and submits one upload. Returns false when the command scope
    /// cannot be opened or recording fails.
    ///
    /// When bGenerateMipmaps is set and generation succeeds, the image ends
    /// fully readable with its declared mip chain; if generation is
    /// unsupported/fails, only the base level is transitioned to finalLayout
    /// and outMipLevels (when provided) receives 1. Otherwise outMipLevels
    /// receives the image's mip level count.
    bool upload(IRender& render, const TextureUploadRequest& request, uint32_t* outMipLevels = nullptr);
};

} // namespace ya
