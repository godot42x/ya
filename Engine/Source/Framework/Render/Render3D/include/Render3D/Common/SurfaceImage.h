#pragma once

#include "Core/Base.h"

#include "Core/Common/Types.h"
#include "RHI/RenderDefines.h"

#include <memory>

namespace ya
{

struct ICommandBuffer;
struct IImageView;
struct RenderTexture;
class RenderSubmission;

/// Which transfer function the numbers in a finished image already carry.
///
/// `EFormat` says how many bits and which channels; it does not say what the
/// values mean, because a UNORM image can hold either linear light or values the
/// display transfer function has already been applied to. The two are not
/// interchangeable at a surface, and the difference is not visible in the
/// format, so it travels with the image instead of being guessed at draw time.
enum class EImageEncoding : uint8_t
{
    /// Linear light. A surface whose format applies the transfer function on
    /// write encodes this exactly once, which is the pairing that wants it.
    Linear,
    /// The transfer function is already baked into the values. Correct on a
    /// surface that stores what it is given; encoding it again would apply the
    /// curve twice.
    DisplayEncoded,
};

/// A finished image together with the one thing its format cannot say, for the
/// pass that writes it into a surface.
struct FSurfaceImage
{
    std::shared_ptr<RenderTexture> image;
    EImageEncoding                 encoding = EImageEncoding::DisplayEncoded;

    /// Whether there is an image at all. Not a check of the resource behind it:
    /// the pass that draws it asks for its view, which is the same question.
    [[nodiscard]] bool hasImage() const { return image != nullptr; }
};

/// Whether writing into a surface of `format` applies the transfer function to
/// whatever the shader produced. True for an sRGB format: the hardware encodes
/// the fragment's output on write, so the values that land in the image are not
/// the values the shader wrote.
[[nodiscard]] inline bool surfaceEncodesOnWrite(EFormat::T format)
{
    return EFormat::isSRGB(format);
}

/// The pairing check for a surface write, as a reason to refuse rather than a
/// bool to ignore: null when the pairing is safe, otherwise what would go wrong.
///
/// Only one of the four pairings is a defect, and it is the one the surface
/// itself would cause: a display-encoded image written into a surface that
/// encodes on write comes out with the transfer function applied twice. A linear
/// image on such a surface is the hardware-encoding path working as intended.
/// Neither image on a surface that stores what it is given is the surface's
/// business -- there the values arrive as the producer made them.
[[nodiscard]] inline const char* findSurfaceImageMismatch(const FSurfaceImage& image,
                                                          EFormat::T           surfaceFormat)
{
    if (image.encoding == EImageEncoding::DisplayEncoded && surfaceEncodesOnWrite(surfaceFormat)) {
        return "display-encoded image on a surface format that applies the transfer function on "
               "write: the hardware would encode it a second time";
    }
    return nullptr;
}

/// Draws a surface image over a surface's declared raster area.
///
/// The surface pass owns the destination and the ordering (its own clear, the
/// host's content on top, then capture); who draws the image is not its
/// business, and no pipeline lives behind this interface's caller. It exists so
/// the surface pass can put a finished image on the window without becoming the
/// place where images are processed -- which is how a presentation path grows a
/// grading stage it was never supposed to have. The implementation is a
/// renderer-side pass (see `SurfaceWritePass`), not a presentation one.
struct ISurfaceBackdropWriter
{
    virtual ~ISurfaceBackdropWriter() = default;

    /// `extent` is the surface's, so the image is stretched across it. The
    /// descriptor set this needs comes from `submission`, which is this frame's
    /// live one: a set written here must not be one an earlier flight's command
    /// buffer is still reading.
    virtual void writeSurfaceBackdrop(ICommandBuffer&   cmdBuf,
                                      RenderSubmission& submission,
                                      IImageView&       image,
                                      Extent2D          extent) = 0;
};

} // namespace ya
