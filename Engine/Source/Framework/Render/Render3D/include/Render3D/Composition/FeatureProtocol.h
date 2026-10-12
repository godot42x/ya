#pragma once

#include "Core/Base.h"
#include "Core/Result.h"
#include "Core/Optional.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Common/ViewTargetStore.h"

#include <cstdint>
#include <optional>
#include <string_view>
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
// Feature concept (C++20)
// ────────────────────────────────────────────────────────────────────────────

struct FeatureResolveContext;
struct FeaturePrepareContext;
struct FeatureGraphContext;

template <typename F>
concept Feature = requires(F feature) {
    typename F::Inputs;
    typename F::Outputs;
    typename F::Resolved;
    typename F::Prepared;

    // resolve(context, inputs, settings) -> Resolved
    { feature.resolve(std::declval<FeatureResolveContext&>(),
                     std::declval<const typename F::Inputs&>(),
                     std::declval<const void*>()) } -> std::same_as<typename F::Resolved>;

    // prepareBindings(context, resolved, preparedInputs) -> Prepared
    { feature.prepareBindings(std::declval<FeaturePrepareContext&>(),
                             std::declval<const typename F::Resolved&>(),
                             std::declval<const void*>()) } -> std::same_as<typename F::Prepared>;

    // append(context, resolved, prepared, inputs) -> Outputs
    { feature.append(std::declval<FeatureGraphContext&>(),
                    std::declval<const typename F::Resolved&>(),
                    std::declval<const typename F::Prepared&>(),
                    std::declval<const void*>()) } -> std::same_as<typename F::Outputs>;
};

// ────────────────────────────────────────────────────────────────────────────
// Contexts (forward declarations, defined in separate headers)
// ────────────────────────────────────────────────────────────────────────────

struct IRender;
struct IRenderResourceFactory;
struct RenderStageContext;
class RenderSubmission;
class ViewTargetStore;
struct ViewTargetLease;

/// Context for feature resolve phase: resource requirements and configuration.
struct FeatureResolveContext
{
    IRender*                 render         = nullptr;
    IRenderResourceFactory*  factory        = nullptr;
    ViewTargetStore*         targetStore    = nullptr;
    uint64_t                 hostTick       = 0;
    uint32_t                 flightIndex    = 0;
};

/// Context for feature prepare phase: allocate bindings using submission resources.
struct FeaturePrepareContext
{
    IRender*                 render         = nullptr;
    RenderSubmission*        submission     = nullptr;
    ViewTargetStore*         targetStore    = nullptr;
    uint64_t                 hostTick       = 0;
    uint32_t                 flightIndex    = 0;
};

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
struct FeatureGraphContext
{
    RenderGraph*             graph          = nullptr;
    IRender*                 render         = nullptr;
    const ViewTargetLease*   viewLease      = nullptr;
    /// The View's recording context this graph belongs to. Features read it for
    /// the frame facts their existing stages already consume, never for live
    /// ECS / active-scene lookups.
    const RenderStageContext* stageCtx      = nullptr;
    uint64_t                 viewId         = 0;
    /// Explicit ordering point for this View's first pass: the previous View's
    /// last pass. Null when another pass (a shadow pass) already took it.
    std::optional<RGPassHandle> predecessor{};
    /// Viewport convention of the pipeline this View records through.
    bool                     bReverseViewportY = true;
};

} // namespace ya
