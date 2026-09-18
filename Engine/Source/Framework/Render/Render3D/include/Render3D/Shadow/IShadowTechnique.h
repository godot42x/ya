#pragma once

#include "Core/Math/Geometry.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/ShadowSettings.h"

#include <cstdint>
#include <memory>

namespace ya
{

struct IRender;
class RenderSubmission;
struct RenderFrameData;
struct IImage;

/// What one View's shadow preparation produced: the View slot its frame
/// resources were prepared into, and the point-light count the passes must
/// honour. Returned by prepare() and handed back at graph-append time, so the
/// technique never has to remember which View it prepared last -- a stored
/// "current View" is already stale for the second View of the same graph.
///
/// The point-light count travels with the token because append() rebuilds the
/// payload from its own frameData, which is not necessarily the packet prepare()
/// saw; keeping it explicit preserves what was actually prepared.
struct ShadowPreparedView
{
    uint32_t viewSlot        = RenderViewRecordingContext::kInvalidViewSlot;
    uint32_t pointLightCount = 0;

    [[nodiscard]] bool valid() const { return viewSlot != RenderViewRecordingContext::kInvalidViewSlot; }
};

// ═══════════════════════════════════════════════════════════════════════════
// IShadowTechnique — Strategy interface for shadow rendering algorithms
//
// Each implementation encapsulates a complete shadow mapping approach:
//   - BasicShadowMapTechnique: standard depth maps (current)
//   - [future] CascadedShadowMapTechnique: CSM for directional lights
//   - [future] VirtualShadowMapTechnique: UE5-style virtual shadow maps
//
// The ShadowStage holds a unique_ptr<IShadowTechnique> and delegates all
// shadow rendering through this interface.
// ═══════════════════════════════════════════════════════════════════════════

struct IShadowTechnique
{
    virtual ~IShadowTechnique() = default;

    /// Initialize GPU resources for this technique.
    virtual void init(IRender* render, const ShadowSettings& settings) = 0;

    /// Release all GPU resources.
    virtual void destroy() = 0;

    /// Apply updated settings (e.g., resolution change, cascade count change).
    /// May trigger resource recreation if resolution changed.
    virtual void applySettings(const ShadowSettings& settings) = 0;

    /// Rebuild technique-side resources/views derived from the current shadow image.
    virtual void refreshShadowResources(const std::shared_ptr<IImage>& depthImage, EFormat::T depthFormat, Extent2D shadowExtent) = 0;

    /// Per-frame data upload (UBOs, instance buffers, frustum data) for one View.
    /// Returns the token the graph-append step must pass back; an invalid token
    /// means this View has no shadow passes to append.
    [[nodiscard]] virtual ShadowPreparedView prepare(RenderSubmission&           submission,
                                                     RenderViewRecordingContext& view) = 0;

};

} // namespace ya
