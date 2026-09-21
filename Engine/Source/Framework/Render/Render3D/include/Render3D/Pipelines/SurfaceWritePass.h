#pragma once

#include "Core/Base.h"
#include "Render3D/Common/SurfaceImage.h"
#include "RHI/RenderDefines.h"

namespace ya
{

struct IRender;
struct BasicPostprocessing;

/// Puts a finished image on the window: the postprocessing family's write onto
/// a surface.
///
/// It is the existing postprocess pipeline run in its pass-through
/// configuration, so there is no second shader, no second pipeline and no state
/// of its own -- and therefore nothing here that can grade an image a second
/// time. "Stretch the display image onto the surface" is a postprocess-shaped
/// job, so it lives with the postprocess pipeline instead of being a second
/// pipeline the presentation path owns.
struct SurfaceWritePass final : ISurfaceBackdropWriter
{
    struct InitDesc
    {
        IRender*   render        = nullptr;
        EFormat::T surfaceFormat = EFormat::Undefined;
    };

    IRender*              _render        = nullptr;
    EFormat::T            _surfaceFormat = EFormat::Undefined;
    stdptr<BasicPostprocessing> _writer;

    void init(const InitDesc& desc);
    void shutdown();

    void writeSurfaceBackdrop(ICommandBuffer&   cmdBuf,
                              RenderSubmission& submission,
                              IImageView&       image,
                              Extent2D          extent) override;

    [[nodiscard]] EFormat::T surfaceFormat() const { return _surfaceFormat; }
};

} // namespace ya
