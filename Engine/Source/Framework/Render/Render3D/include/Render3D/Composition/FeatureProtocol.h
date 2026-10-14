#pragma once

#include "Core/Base.h"
#include "Core/Log.h"
#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Stage/IRenderStage.h"
#include "Render3D/Common/ViewTargetStore.h"

#include <concepts>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace ya
{

struct IRender;
struct IDescriptorSetLayout;
struct DebugRenderSystem;

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
} && requires(const F&                                     feature,
              const typename F::Resolved&                  resolved,
              std::vector<ViewAttachmentDesc>&             targetOut) {
    { feature.name() } -> std::same_as<std::string_view>;
    { feature.targets(resolved, targetOut) } -> std::same_as<void>;
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

/// One feature's per-phase values for one View. Recipes hold a tuple of these
/// keyed by feature type, so adding a feature adds one tuple member and its
/// wiring; nothing else in the recipe is per-feature.
template <typename F>
struct Invocation
{
    using Resolved     = F::Resolved;
    using Prepared     = F::Prepared;
    using GraphOutputs = F::GraphOutputs;

    Resolved     resolved{};
    Prepared     prepared{};
    GraphOutputs outputs{};
};

/// Tuple-wise value copy for the phase structs that hold Invocations by value
/// but expose named references (their members are not assignable as a whole).
template <typename... F>
void assignInvocations(std::tuple<Invocation<F>...>&       to,
                       const std::tuple<Invocation<F>...>& from)
{
    (..., static_cast<void>(std::get<Invocation<F>>(to) = std::get<Invocation<F>>(from)));
}

// ────────────────────────────────────────────────────────────────────────────
// Invocation tuple folds
// ────────────────────────────────────────────────────────────────────────────

/// Every declared target, folded in map order. A feature not in the tuple is
/// not in the fold, so nothing can be forgotten here: the tuple is the
/// registration.
template <typename... F>
void foldTargets(const std::tuple<Invocation<F>...>& invocations, std::vector<ViewAttachmentDesc>& out)
{
    (..., F::targets(std::get<Invocation<F>>(invocations).resolved, out));
}

/// One line per feature, folded in map order with each feature's own name and
/// enablement. No lambda: the fold body runs as a plain comma expression so
/// there is no capture to get wrong and nothing can swallow the expansion.
template <typename... F>
void foldDescribe(const std::tuple<Invocation<F>...>& invocations, std::string& out)
{
    ((out += std::format("  {} {}\n",
                         std::get<Invocation<F>>(invocations).resolved.bEnabled ? "[on] " : "[off]",
                         F::name())),
     ...);
}

// ────────────────────────────────────────────────────────────────────────────
// Phase steps: the folding helpers a recipe's schedule is written with
// ────────────────────────────────────────────────────────────────────────────

/// Run one feature's `prepareBindings` as a step of a recipe's prepare phase.
///
/// The step owns the part every phase has repeated: call, check, store, and a
/// log that names the feature. The per-phase contexts stay the feature's own
/// typed type -- the ports are the point of the protocol, so folding must not
/// dissolve them. `outPrepared` is written only on success.
template <typename F>
[[nodiscard]] Result<void, FeatureError> prepareFeature(const F&                           feature,
                                                        const typename F::Resolved&        resolved,
                                                        const typename F::PrepareContext&  ctx,
                                                        typename F::Prepared&              outPrepared)
{
    auto result = feature.prepareBindings(resolved, ctx);
    if (result.isErr()) {
        YA_CORE_ERROR("{} prepare failed, error={}", F::name(), static_cast<int>(result.error()));
        return result.error();
    }
    outPrepared = std::move(result.value());
    return Result<void, FeatureError>::success();
}

/// Run one feature's `append` as a step of a recipe's append phase.
///
/// `stage` is the device object the feature's pass draws through; the concept
/// still carries it as a parameter (removing that parameter is what the
/// feature-owned-device work is for), so a recipe states it here exactly as it
/// states the typed facts/inputs. `outOutputs` is written only on success.
template <typename F>
[[nodiscard]] Result<void, FeatureError> appendFeature(const F&                          feature,
                                                       RenderGraph&                      graph,
                                                       typename F::Stage*                stage,
                                                       const FeatureGraphContext&        graphCtx,
                                                       const typename F::Facts&          facts,
                                                       const typename F::Resolved&       resolved,
                                                       const typename F::Prepared&       prepared,
                                                       const typename F::Inputs&         inputs,
                                                       typename F::GraphOutputs&         outOutputs)
{
    auto result = feature.append(graph, stage, graphCtx, facts, resolved, prepared, inputs);
    if (result.isErr()) {
        YA_CORE_ERROR("{} append failed, error={}", F::name(), static_cast<int>(result.error()));
        return result.error();
    }
    outOutputs = std::move(result.value());
    return Result<void, FeatureError>::success();
}

// ────────────────────────────────────────────────────────────────────────────
// Feature lifecycle: init / shutdown / beginFrame
// ────────────────────────────────────────────────────────────────────────────

/// The shared dependencies a feature may take at init time.
///
/// One context, each feature takes what it needs and ignores the rest: the
/// pipeline states the environment once instead of knowing every feature's
/// individual init signature. A feature that needs nothing from it (a graph-only
/// feature) simply does not read it.
///
/// This is a plain aggregate, deliberately not a service locator: the fields are
/// the concrete collaborators the pipeline already holds, and a feature reads
/// them by name. No type erasure, no lookup by string, no registry.
struct FeatureInitContext
{
    IRender*                     render = nullptr;
    /// The frame/light descriptor layout the deferred GBuffer and lighting
    /// passes share.
    stdptr<IDescriptorSetLayout> frameAndLightDSL;
    /// The skinning layout the GBuffer pass's skinned draws read.
    stdptr<IDescriptorSetLayout> skinningDSL;
    /// The environment-lighting layout; empty when the Scene has no resolved
    /// environment.
    stdptr<IDescriptorSetLayout> environmentLightingDSL;
    /// The debug overlay sink the overlay pass draws through.
    DebugRenderSystem*           debugRenderSystem = nullptr;
    /// The finalize pass's output format (each pipeline states its own).
    EFormat::T                   postProcessColorFormat = EFormat::Undefined;
};

/// The lifecycle a feature participates in through the unified context.
///
/// A feature that owns a device object implements all three; one that owns
/// nothing (a graph-only feature) is not required to -- `initFeatures` below
/// only visits features that declare the member.
///
/// The reference is non-const on purpose: lifecycle mutates the feature (it
/// creates and tears down its device object). A `const F&` here would make the
/// concept false for every real feature and the folds would silently skip them
/// all.
template <typename F>
concept LifecycleFeature = requires(F& feature, const FeatureInitContext& ctx) {
    { feature.init(ctx) } -> std::same_as<void>;
    { feature.shutdown() } -> std::same_as<void>;
    { feature.beginFrame() } -> std::same_as<void>;
};

/// One lifecycle fold over a heterogeneous feature list, in the order given.
///
/// A fold, not a base-class array: each feature is visited at its own static
/// type, so nothing is erased. Every argument must satisfy `LifecycleFeature`:
/// a feature whose lifecycle signature drifts fails to compile here instead of
/// being silently skipped, which is the failure mode a conditional fold would
/// hide (the first version of this used `if constexpr` and quietly initialized
/// nothing when the concept was mis-written).
template <typename... F>
void initFeatures(const FeatureInitContext& ctx, F&... features)
{
    static_assert((LifecycleFeature<F> && ...), "every feature in a lifecycle fold must implement init/shutdown/beginFrame");
    (..., features.init(ctx));
}

/// Shutdown in reverse order: a feature initialized earlier may be what a later
/// one's device object points at, so teardown mirrors construction.
template <typename... F>
void shutdownFeatures(F&... features)
{
    static_assert((LifecycleFeature<F> && ...), "every feature in a lifecycle fold must implement init/shutdown/beginFrame");
    (..., features.shutdown());
}

/// Submission-scoped warmup fold, in list order.
template <typename... F>
void beginFrameFeatures(F&... features)
{
    static_assert((LifecycleFeature<F> && ...), "every feature in a lifecycle fold must implement init/shutdown/beginFrame");
    (..., features.beginFrame());
}

} // namespace ya
