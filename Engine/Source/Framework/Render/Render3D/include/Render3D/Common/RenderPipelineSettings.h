#pragma once

#include "Render3D/Common/PostProcessingState.h"
#include "Render3D/Common/ShadowSettings.h"

#include <cstdint>

namespace ya
{

/// Which render strategy a pipeline is. It is the pipeline's identity, so a
/// caller can ask what it is dealing with without downcasting into a concrete
/// `ForwardRenderPipeline` / `DeferredRenderPipeline` to find out.
enum class ERenderPipelineKind : uint8_t
{
    Forward,
    Deferred,
};

[[nodiscard]] inline const char* toString(ERenderPipelineKind kind)
{
    return kind == ERenderPipelineKind::Forward ? "Forward" : "Deferred";
}

/// The settings surface of a render strategy, as one value.
///
/// The two blocks are not symmetrical and the type says which is which: `shadow`
/// and `postProcessing` are read by both strategies, while the strategy-specific
/// switches below are read by the deferred one. A forward pipeline carries the
/// block it was handed and reads none of it -- which `kind` states, so the editor
/// gates its deferred-only controls on `kind` instead of asking which concrete
/// pipeline object is active.
///
/// This replaced `DeferredRenderPipeline::SettingsSnapshot` plus Forward's own
/// post/shadow accessors. The editor used to reach those through a `dynamic_cast`
/// to the concrete pipeline, which made "what settings exist" a question about
/// the class hierarchy rather than about the renderer.
struct RenderPipelineSettings
{
    ERenderPipelineKind kind = ERenderPipelineKind::Deferred;

    ShadowSettings      shadow{};
    PostProcessingState postProcessing{};

    // ── Read by the deferred strategy ─────────────────────────────────
    bool  bReverseViewportY = true;
    bool  bSSAOEnabled      = true;
    float ssaoRadius        = 0.6f;
    float ssaoBias          = 0.025f;
    float ssaoPower         = 1.5f;
    float ssaoIntensity     = 2.5f;
    bool  bPBRDiffuseIBL    = true;
    bool  bPBRSpecularIBL   = true;
};

} // namespace ya
