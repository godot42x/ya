#pragma once

#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Render3D/Composition/FeatureProtocol.h"

#include <concepts>
#include <string_view>
#include <tuple>

namespace ya
{

// ────────────────────────────────────────────────────────────────────────────
// Named extension points
// ────────────────────────────────────────────────────────────────────────────

/// The fixed positions in the Deferred schedule where a new effect may attach.
///
/// A point is a *name for a place*, not a registry: the recipe runs whatever
/// extension declares it belongs there, and the extension gets a typed context
/// of exactly what that place can offer. This is the piece the core feature list
/// does not give -- a core feature is "in the map", but nothing states *where*
/// an optional effect wants to sit relative to the core passes.
enum class DeferredExtensionPoint
{
    /// After the GBuffer, before lighting. Custom AO, thickness maps.
    AfterGBuffer,
    /// After lighting, before transparent content. Reflections, GI.
    AfterLighting,
    /// Scene complete, before post-processing. Custom resolves.
    BeforePostProcess,
    /// Inside the post-process chain. Colour grades, filters.
    PostProcessChain,
};

/// The typed inputs a point offers. Each point fills the handles it can offer; a
/// feature reads what its point provides and ignores the rest.
///
/// `color` is read/write: it is the colour stream entering the point, and an
/// extension replaces it by assigning to `ctx.color`. That threading is the
/// reason an extension does not need the recipe to know its output shape -- the
/// stream is the same handle type at every point.
struct ExtensionContext
{
    RenderGraph&               graph;
    const FeatureGraphContext& graphCtx;
    Extent2D                   viewExtent{};
    /// In/out: the colour stream at this point.
    RGTextureHandle color{};
    /// The scene depth; available from AfterGBuffer onward.
    RGTextureHandle depth{};
    /// The GBuffer world normal; available from AfterGBuffer onward.
    RGTextureHandle normal{};

    [[nodiscard]] uint64_t viewId() const { return graphCtx.viewId; }
};

/// The protocol an extension implements.
///
/// The same shape as a core `Feature` for the phases that matter (resolve,
/// prepareBindings) -- so an extension gets the same settings-freeze and
/// per-submission-binding guarantees -- minus the two things a core feature has
/// that an extension does not need:
/// - no `targets()`: an extension writes into a transient it owns or into the
///   colour stream, so it declares no View attachment;
/// - no `GraphOutputs`: its product is the colour stream it threaded, not a
///   typed struct the recipe has to name.
template <typename F>
concept ExtensionFeature = requires {
    typename F::Settings;
    typename F::Facts;
    typename F::Inputs;
    typename F::Resolved;
    typename F::Prepared;
    typename F::PrepareContext;
} && requires(const F&                        feature,
              const typename F::Settings&      settings,
              const typename F::Facts&         facts,
              const typename F::Inputs&        inputs,
              const typename F::Resolved&      resolved,
              const typename F::Prepared&      prepared,
              const typename F::PrepareContext& prepareCtx,
              ExtensionContext&                extCtx) {
    { F::extensionPoint() } -> std::same_as<DeferredExtensionPoint>;
    { F::name() } -> std::same_as<std::string_view>;
    { feature.resolve(settings, facts, inputs) } -> std::same_as<typename F::Resolved>;
    {
        feature.prepareBindings(resolved, prepareCtx)
    } -> std::same_as<Result<typename F::Prepared, FeatureError>>;
    // The context is non-const: appending threads the colour stream, which means
    // writing `ctx.color`. A const context would make the concept false for every
    // extension and the fold would silently skip them all.
    { feature.appendExtension(extCtx, resolved, prepared) } -> std::same_as<Result<void, FeatureError>>;
};

// ────────────────────────────────────────────────────────────────────────────
// Extension folds
// ────────────────────────────────────────────────────────────────────────────

/// The long-lived extension instances the pipeline owns.
///
/// Unlike a core feature -- which is stateless in prepare/append and receives
/// its device pointer from the recipe -- an extension owns a device object, so
/// it must be invoked on the pipeline's instance, never on a freshly
/// constructed temporary (that temporary's device pointer is null: the same
/// stack-temporary failure the core features already fixed). The fold therefore
/// matches each extension type to its instance by type, with no erasure.
template <typename... E>
struct ExtensionInstances
{
    std::tuple<E*...> instances{};
};

namespace detail
{

/// The instance of exactly type `F` among `instances`, or null.
template <typename F, typename... E>
[[nodiscard]] F* findExtensionInstance(std::tuple<E*...>& instances)
{
    F* found = nullptr;
    (..., (std::is_same_v<E, F> ? (found = std::get<E*>(instances)) : nullptr));
    return found;
}

} // namespace detail

/// Run every extension that declares `Point`, in tuple order, threading the
/// colour stream through `ctx.color`.
///
/// A fold, not a runtime list: each extension is visited at its own static type
/// and only those whose declared point matches actually run. Adding a second
/// extension at a point is one tuple entry -- the recipe's schedule does not
/// change, and neither does the fold.
template <DeferredExtensionPoint Point, typename... F, typename... E>
[[nodiscard]] Result<void, FeatureError> appendExtensionsAt(ExtensionContext&                    ctx,
                                                            const std::tuple<Invocation<F>...>&  resolvedExts,
                                                            const std::tuple<Invocation<F>...>&  preparedExts,
                                                            ExtensionInstances<E...>&            instances)
{
    Result<void, FeatureError> status = Result<void, FeatureError>::success();
    (..., [&]() {
        if (status.isErr()) {
            return;
        }
        using Ext = F;
        if constexpr (Ext::extensionPoint() == Point) {
            Ext* instance = detail::findExtensionInstance<Ext>(instances.instances);
            if (instance == nullptr) {
                YA_CORE_ERROR("{} extension has no instance registered", Ext::name());
                status = FeatureError::MissingContext;
                return;
            }
            // Resolved and Prepared are two structs, exactly as they are for a
            // core feature: reading `.prepared` off the resolved tuple would
            // hand the fold an empty binding (the mistake this signature
            // exists to make unspellable).
            const auto& resolved = std::get<Invocation<Ext>>(resolvedExts).resolved;
            const auto& prepared = std::get<Invocation<Ext>>(preparedExts).prepared;
            auto        result   = instance->appendExtension(ctx, resolved, prepared);
            if (result.isErr()) {
                YA_CORE_ERROR("{} extension append failed, error={}", Ext::name(), static_cast<int>(result.error()));
                status = result.error();
            }
        }
    }());
    return status;
}

} // namespace ya
