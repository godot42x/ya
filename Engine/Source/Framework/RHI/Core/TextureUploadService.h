#pragma once

#include "RHI/RenderDefines.h"

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

/// Incremental update of a sub-rectangle of an ALREADY-UPLOADED image.
///
/// Reuses the existing VkImage (must have TransferDst usage, which fromData/
/// initFromData always set). Does NOT recreate the image and does NOT generate
/// mipmaps — callers that need mip regeneration must re-upload fully. Intended
/// for append-mostly atlases (font glyphs) where only a few cells changed.
struct TextureRegionUpdateRequest
{
    std::shared_ptr<IImage> image{};
    std::shared_ptr<IBuffer> staging{};
    BufferImageCopy          region{}; // single sub-rectangle, offset = top-left
    EImageLayout::T          currentLayout = EImageLayout::ShaderReadOnlyOptimal;
    std::string              label;
};

/// Batched incremental update of MULTIPLE sub-rectangles of an ALREADY-UPLOADED
/// image, using ONE staging buffer and ONE isolate submit.
///
/// `regions` must be tightly backed by `staging`: each region's bufferOffset
/// points at that sub-rectangle's pixels inside the single buffer. The whole
/// batch shares one ShaderReadOnlyOptimal -> TransferDst -> ShaderReadOnlyOptimal
/// transition, so callers appending many glyphs per frame should batch instead
/// of calling updateRegion() per glyph (avoids N staging buffers + N submits).
struct TextureRegionUpdateBatch
{
    std::shared_ptr<IImage>  image{};
    std::shared_ptr<IBuffer> staging{};
    std::vector<BufferImageCopy> regions{}; // multiple sub-rectangles
    EImageLayout::T          currentLayout = EImageLayout::ShaderReadOnlyOptimal;
    std::string              label;
};

/// Owns texture upload command recording and submission.
///
/// Explicitly depends on IRender for the resource factory and isolate-command
/// submission; it never reaches for a global render or App.
struct YA_RHI_API TextureUploadService
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

    /// Records and submits a single sub-rectangle copy into an existing image.
    /// Transitions currentLayout -> TransferDst -> ShaderReadOnlyOptimal inline.
    /// Returns false if the command scope cannot be opened or recording fails.
    bool updateRegion(IRender& render, const TextureRegionUpdateRequest& request);

    /// Batched variant: `batch.regions` are all copied in a single isolate
    /// submit with one TransferDst-layout transition. Returns false if the
    /// batch is empty or malformed; never partial — either the whole batch
    /// lands or nothing is copied.
    bool updateRegions(IRender& render, const TextureRegionUpdateBatch& batch);
};

} // namespace ya
