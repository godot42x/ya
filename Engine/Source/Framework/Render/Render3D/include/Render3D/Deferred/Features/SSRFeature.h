#pragma once

#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Composition/ExtensionProtocol.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Deferred/SSRStage.h"

#include <optional>

namespace ya
{

struct RenderFrameData;
class RenderSubmission;

/// Screen-space reflection: a real new effect used to validate the extension
/// point protocol.
///
/// It runs at `DeferredExtensionPoint::AfterLighting`: it reads the lit colour,
/// the GBuffer world normal and the depth, marches a view-space reflection ray
/// against the depth buffer, and replaces the colour stream with the reflection
/// result. Everything downstream (the transparent slot, skybox, sprites)
/// continues the replacement stream -- which is what "an extension may replace
/// the colour" means.
///
/// The feature is deliberately a template, not a special case: it implements the
/// `ExtensionFeature` protocol (resolve / prepareBindings / appendExtension) and
/// owns its device object, so it gets the same correctness guarantees as a core
/// feature -- while its wiring is the extension tuple plus one row at its point,
/// not a named member on every phase struct.
struct SSRFeature
{
    using Stage = SSRStage;

    struct Settings
    {
        bool     bEnabled    = false;
        float    maxDistance = 24.0f;
        float    thickness   = 0.6f;
        float    intensity   = 1.0f;
        uint32_t stepCount   = 48;
    };

    struct Facts
    {
        Extent2D               viewExtent{};
        const RenderFrameData* frameData = nullptr;
        bool                   bReverseY = true;
    };

    /// Typed data flow into the pass: the lit colour it reflects from, plus the
    /// GBuffer normal and depth it marches against.
    struct Inputs
    {
        RGTextureHandle sceneColor{};
        RGTextureHandle normal{};
        RGTextureHandle depth{};
    };

    struct Resolved
    {
        bool     bEnabled = false;
        /// The frame UBO payload for this View, built in resolve so the upload
        /// and the shader see one value.
        SSRStage::FrameData frameData{};
        /// The View colour format the reflection pass writes in.
        EFormat::T outputFormat = EFormat::Undefined;
    };

    struct PrepareContext
    {
        RenderSubmission* submission = nullptr;
        IRender*          render     = nullptr;
        SSRStage*         stage      = nullptr;
        uint32_t          alignment  = 1;
    };

    struct Prepared
    {
        bool                     bEnabled = false;
        /// This View's SSR frame UBO: upload slice + its own descriptor set.
        UniformBufferPassBinding frame;
        DescriptorSetHandle      inputDS{};
    };

    /// Extensions thread the colour stream rather than returning a typed output
    /// struct, but `Invocation<F>` (the shared phase storage) is keyed on
    /// `GraphOutputs`, so an extension states an empty one. It carries no data
    /// and is never read -- the colour stream is the extension's product.
    struct GraphOutputs
    {
    };

    /// The device object this feature owns; the pipeline drives its lifecycle.
    stdptr<SSRStage> _stage;
    /// Authoritative settings, held by the owner of the device object (the
    /// pipeline's long-lived instance), like every other device feature.
    Settings _settings{};

    [[nodiscard]] Settings& settings() { return _settings; }
    [[nodiscard]] const Settings& settings() const { return _settings; }

    void init(const FeatureInitContext& ctx);
    void shutdown();
    void beginFrame();
    void refreshDeviceFormats(EFormat::T colorFormat);

    [[nodiscard]] SSRStage* stage() const { return _stage.get(); }

    [[nodiscard]] Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const;

    [[nodiscard]] Result<Prepared, FeatureError> prepareBindings(const Resolved&       resolved,
                                                                 const PrepareContext& ctx) const;

    /// Run at the AfterLighting point: reads `ctx.color` (the lit scene), writes
    /// the reflection back into `ctx.color`. Disabled, it leaves the stream
    /// untouched -- a consumer never asks whether SSR ran.
    [[nodiscard]] Result<void, FeatureError> appendExtension(ExtensionContext& ctx,
                                                             const Resolved&   resolved,
                                                             const Prepared&   prepared) const;

    [[nodiscard]] static constexpr DeferredExtensionPoint extensionPoint() { return DeferredExtensionPoint::AfterLighting; }
    [[nodiscard]] static constexpr std::string_view name() { return "SSRFeature"; }
};

static_assert(ExtensionFeature<SSRFeature>, "SSRFeature must satisfy the extension protocol");

} // namespace ya
