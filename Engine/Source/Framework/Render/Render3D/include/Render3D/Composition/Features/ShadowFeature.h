#pragma once

#include "Graph/RenderGraph.h"
#include "Render3D/Common/Shadow/ShadowGraphOutputs.h"
#include "Render3D/Common/Shadow/ShadowStage.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Shadow/IShadowTechnique.h"

namespace ya
{

/// Shadow as a composition contribution.
///
/// Shadow is not a View target pass, and deliberately so: the strategy owns the
/// shadow map image, the technique owns the cascade/face/cull bindings, and the
/// budget lives in ShadowSettings. What this feature contributes is the typed
/// path: it resolves whether this View has shadow content, prepares the View's
/// shadow frame resources into a token, and appends the technique's passes.
///
/// Both world recipes run it first, before their own map, because shadow passes
/// take the family ordering point when they add any pass.
struct ShadowFeature
{
    using Stage = ShadowStage;

    struct Settings
    {
        bool bEnabled = false;
    };

    /// No per-View facts: the View's identity is the token prepare produces
    /// (the technique's own view slot), not something the caller states twice.
    struct Facts
    {
    };

    struct Inputs
    {
    };

    struct Resolved
    {
        bool bEnabled = false;
    };

    struct PrepareContext
    {
        RenderSubmission* submission = nullptr;
        ShadowStage*      stage      = nullptr;
        /// The View to prepare shadow resources for. By value: the technique
        /// assigns the View its own shadow view slot, and that slot belongs to
        /// the returned token, not to the caller's context.
        RenderViewRecordingContext view{};
    };

    struct Prepared
    {
        ShadowPreparedView view{};

        [[nodiscard]] bool isValid() const { return view.valid(); }
    };

    struct GraphOutputs
    {
        ShadowGraphOutputs outputs{};
    };

    [[nodiscard]] Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const;

    [[nodiscard]] Result<Prepared, FeatureError> prepareBindings(const Resolved&       resolved,
                                                                 const PrepareContext& ctx) const;

    [[nodiscard]] Result<GraphOutputs, FeatureError> append(RenderGraph&               graph,
                                                            Stage*                     stage,
                                                            const FeatureGraphContext& ctx,
                                                            const Facts&               facts,
                                                            const Resolved&            resolved,
                                                            const Prepared&            prepared,
                                                            const Inputs&              inputs) const;
};

static_assert(Feature<ShadowFeature>, "ShadowFeature must satisfy the composition Feature protocol");

} // namespace ya
