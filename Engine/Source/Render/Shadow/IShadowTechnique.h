#pragma once

#include "Core/Math/Geometry.h"
#include "Render/RenderDefines.h"
#include "ShadowSettings.h"

#include <cstdint>
#include <memory>

namespace ya
{

struct IRender;
struct ICommandBuffer;
struct RenderFrameData;
struct IImage;

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

    /// Per-frame data upload (UBOs, instance buffers, frustum data).
    virtual void prepare(uint32_t flightIndex, const RenderFrameData& frameData) = 0;

    /// Record GPU commands (compute dispatch, render passes).
    virtual void execute(ICommandBuffer* cmdBuf, uint32_t flightIndex, const RenderFrameData& frameData) = 0;

    /// ImGui debug panel for this technique.
    virtual void renderGUI() = 0;
};

} // namespace ya
