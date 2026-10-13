#pragma once

#include "Graph/RenderGraph.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Forward/ForwardViewAuxPasses.h"

#include <string_view>

namespace ya
{

struct RenderFrameData;
class RenderSubmission;

/// Forward skybox: continues the opaque pass's color with the Scene's skybox,
/// reading the depth the opaque pass wrote so geometry occludes it.
///
/// Forward's skybox keeps the View's color attachable (the transparent and
/// entity-id passes still attach it), so this pass declares its own attachments
/// rather than going through the shared color-continuation contract. Its frame
/// UBO is built in resolve, uploaded with this feature's prepared binding, and
/// imported by append; the cubemap packet comes from Facts.
struct ForwardSkyboxFeature
{
    using Stage = ForwardViewAuxPasses;

    struct Settings
    {
        bool             bEnabled = true;
        std::string_view passName{};
    };

    struct Facts
    {
        Extent2D                                   viewExtent{};
        const RenderFrameData*                     frameData = nullptr;
        const ForwardViewAuxPasses::DrawContext::SkyboxInput* skybox = nullptr;
    };

    struct Inputs
    {
        RGTextureHandle color{};
        RGTextureHandle depth{};
        EImageLayout::T colorFinalLayout = EImageLayout::ColorAttachmentOptimal;
    };

    struct Resolved
    {
        bool                         bEnabled = false;
        std::string_view             passName{};
        ForwardViewAuxPasses::SkyboxFrameUBO frameData{};
    };

    struct PrepareContext
    {
        RenderSubmission*  submission = nullptr;
        IRender*           render     = nullptr;
        ForwardViewAuxPasses* stage   = nullptr;
        uint32_t           alignment  = 1;
    };

    struct Prepared
    {
        /// This View's skybox frame UBO: upload slice + its own descriptor set.
        UniformBufferPassBinding frame;
    };

    struct GraphOutputs
    {
        RGTextureHandle color{};
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
    [[nodiscard]] static constexpr std::string_view name() { return "ForwardSkyboxFeature"; }
    static void targets(const Resolved& resolved, std::vector<ViewAttachmentDesc>& out) { (void)resolved; }
};

static_assert(Feature<ForwardSkyboxFeature>, "ForwardSkyboxFeature must satisfy the composition Feature protocol");

} // namespace ya
