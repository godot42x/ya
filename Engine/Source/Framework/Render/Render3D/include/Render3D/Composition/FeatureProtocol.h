#pragma once

#include "Core/Base.h"
#include "Core/Result.h"
#include "Core/Optional.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Stage/IRenderStage.h"
#include "Render3D/Common/ViewTargetStore.h"

#include <concepts>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace ya
{

// ────────────────────────────────────────────────────────────────────────────
// Feature errors
// ────────────────────────────────────────────────────────────────────────────

enum class FeatureError
{
    MissingDependency,
    MissingContext,
    AllocationFailed,
    InvalidInput,
};

// ────────────────────────────────────────────────────────────────────────────
// Typed resource references (use Core::OptionalHandle<T>)
// ────────────────────────────────────────────────────────────────────────────

/// Type-safe texture reference in composition.
/// Use OptionalHandle<T> from Core/Optional.h for typed handle references.
/// Not to be confused with Core/AssetRef.h TextureRef (asset path reference).
template <typename T>
using CompositionTextureRef = OptionalHandle<T>;

/// Type-safe buffer reference in composition.
template <typename T>
using CompositionBufferRef = OptionalHandle<T>;

/// Reference to another feature's prepared bindings.
template <typename T>
using CompositionPreparedRef = OptionalHandle<T>;

// ────────────────────────────────────────────────────────────────────────────
// Composition tags (empty types for typed refs)
// ────────────────────────────────────────────────────────────────────────────

namespace composition_tag
{

struct GBufferAlbedo {};
struct GBufferNormal {};
struct GBufferMaterial {};
struct GBufferEmissive {};
struct Depth {};
struct Color {};
struct DisplayColor {};
struct AmbientOcclusion {};
struct AO {}; // Alias for AmbientOcclusion
struct Shadow {};
struct FrameUBO {};
struct LightUBO {};

} // namespace composition_tag

// ────────────────────────────────────────────────────────────────────────────
// Typed aliases
// ────────────────────────────────────────────────────────────────────────────

using GBufferAlbedo = composition_tag::GBufferAlbedo;
using GBufferNormal = composition_tag::GBufferNormal;
using GBufferMaterial = composition_tag::GBufferMaterial;
using GBufferEmissive = composition_tag::GBufferEmissive;
using Depth = composition_tag::Depth;
using Color = composition_tag::Color;
using AO = composition_tag::AO;

// Note: OptionalAO moved to SSAOFeature::OptionalAO (using OptionalHandle<ssao_tag::AO>)
// Each feature defines its own optional output types for type safety.

// ────────────────────────────────────────────────────────────────────────────
// Contexts
// ────────────────────────────────────────────────────────────────────────────

/// One View target a feature needs allocated: the composition turns these into
/// ViewTargetRequest attachments, and the feature's append imports the same role
/// from its View's lease. One declaration, both uses.
struct ViewTargetRequirement
{
    EViewAttachment role    = EViewAttachment::SceneColor;
    EFormat::T      format  = EFormat::Undefined;
    EImageUsage::T  usage   = EImageUsage::None;
    ESampleCount::T samples = ESampleCount::Sample_1;
    bool            isDepth = false;

    [[nodiscard]] ViewAttachmentDesc toDesc() const
    {
        return ViewAttachmentDesc{
            .role    = role,
            .format  = format,
            .usage   = usage,
            .samples = samples,
            .isDepth = isDepth,
        };
    }
};

/// Context for feature graph append phase: add RDG passes.
///
/// The graph is a parameter of `append`; this carries only the View-local
/// recording inputs: its leased targets, its recording context, identity and
/// ordering point. The recording context is `RenderStageContext`, which has no
/// Scene / ECS handle, so a graph callback cannot answer "which Scene is
/// current" even by accident.
struct FeatureGraphContext
{
    const ViewTargetLease*   viewLease = nullptr;
    /// The View's recording context this graph belongs to. Features read it for
    /// the frame facts their existing stages consume.
    const RenderStageContext* stageCtx = nullptr;
    uint64_t                 viewId    = 0;
    /// Explicit ordering point for this View's first pass: the previous View's
    /// last pass. Null when another pass (a shadow pass) already took it.
    std::optional<RGPassHandle> predecessor{};
    /// Viewport convention of the pipeline this View records through.
    bool                     bReverseViewportY = true;
};

// ────────────────────────────────────────────────────────────────────────────
// Feature protocol (C++20)
// ────────────────────────────────────────────────────────────────────────────

/// The uniform shape every composition feature implements.
///
/// `resolve` is pure: settings + immutable facts + declared inputs in, `Resolved`
/// out (resource requirements and per-View values). `prepareBindings` allocates
/// the feature's descriptor sets and upload slices from the live submission.
/// `append` adds the feature's real RDG pass(es) and returns the handles the
/// later features consume.
///
/// Enforced by `static_assert(Feature<X>)` in each feature's translation unit
/// and in `FeatureProtocolTest`: a signature that drifts fails to compile
/// instead of silently no longer being a feature. The type requirements are the
/// first conjunct so a feature missing one of them is simply "not a Feature"
/// rather than an error inside the expression below.
template <typename F>
concept Feature = requires {
    typename F::Settings;
    typename F::Facts;
    typename F::Inputs;
    typename F::Resolved;
    typename F::Prepared;
    typename F::GraphOutputs;
    typename F::PrepareContext;
    typename F::Stage;
} && requires(const F& feature, RenderGraph& graph, const FeatureGraphContext& graphContext) {
    {
        feature.resolve(std::declval<const typename F::Settings&>(),
                        std::declval<const typename F::Facts&>(),
                        std::declval<const typename F::Inputs&>())
    } -> std::same_as<typename F::Resolved>;

    {
        feature.prepareBindings(std::declval<const typename F::Resolved&>(),
                                std::declval<const typename F::PrepareContext&>())
    } -> std::same_as<Result<typename F::Prepared, FeatureError>>;

    {
        feature.append(graph,
                       static_cast<typename F::Stage*>(nullptr),
                       graphContext,
                       std::declval<const typename F::Facts&>(),
                       std::declval<const typename F::Resolved&>(),
                       std::declval<const typename F::Prepared&>(),
                       std::declval<const typename F::Inputs&>())
    } -> std::same_as<Result<typename F::GraphOutputs, FeatureError>>;
};

} // namespace ya
