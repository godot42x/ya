#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Graph/RenderGraph.h"
#include "Runtime/Rendering/Common/Shadow/ShadowGraphOutputs.h"
#include "Runtime/Rendering/Deferred/ViewportOverlayStage.h"

#include <array>
#include <memory>
#include <optional>

namespace ya
{

namespace deferred_graph_exports
{

inline constexpr std::string_view gBufferColor[4] = {
    "Deferred.GBuffer.Color0",
    "Deferred.GBuffer.Color1",
    "Deferred.GBuffer.Color2",
    "Deferred.GBuffer.Color3",
};
inline constexpr std::string_view gBufferDepth  = "Deferred.GBuffer.Depth";
inline constexpr std::string_view viewportColor = "Deferred.Viewport.Color";
inline constexpr std::string_view ssao          = "Deferred.SSAO.Output";
inline constexpr std::string_view entityId      = "Deferred.Viewport.EntityId";

} // namespace deferred_graph_exports

struct ICommandBuffer;

// Frame-local logical resources used by the Deferred graph. Handles are valid
// only for the graph that produced them; resolved GPU objects must stay inside
// the corresponding execution callback.
struct DeferredFrameGraphResources
{
    struct Buffers
    {
        RGBufferHandle                frame{};
        RGBufferHandle                light{};
        RGBufferHandle                skinning{};
        std::optional<RGBufferHandle> ssaoFrame{};
        RGBufferHandle                skyboxFrame{};
    } buffers{};

    struct Textures
    {
        std::array<RGTextureHandle, 4> gBufferColors{};
        RGTextureHandle                 gBufferDepth{};
        RGTextureHandle                 viewportColor{};
        std::optional<RGTextureHandle>  ssao{};
        std::optional<RGTextureHandle>  environmentCubemap{};
        std::optional<RGTextureHandle>  environmentIrradiance{};
        std::optional<RGTextureHandle>  environmentPrefilter{};
        std::optional<RGTextureHandle>  environmentBrdfLut{};
        std::optional<RGTextureHandle>  shadowDepth{};
        std::optional<RGTextureHandle>  bloomComposite{};
        RGTextureHandle                 overlayInput{};
        std::optional<RGTextureHandle>  postprocessOutput{};
        RGTextureHandle                 entityId{};
    } textures{};

    struct Passes
    {
        ShadowGraphOutputs          shadow{};
        std::optional<RGPassHandle> gBuffer{};
        std::optional<RGPassHandle> light{};
        std::optional<RGPassHandle> skybox{};
        std::optional<RGPassHandle> sceneOverlay{};
        std::optional<RGPassHandle> viewportOverlay{};
    } passes{};
};

/// Typed pass parameters for the Deferred GBuffer graph pass.
///
/// One object drives both graph setup (declaration) and execute (resolve), so
/// the pass never reads binding state back from a stage member. Handles are
/// frame-local; the current-flight descriptor binding is owned by
/// DeferredFrameResourceSet and carried here only for the execute callback.
struct DeferredGBufferPassParams
{
    struct BufferInput
    {
        RGBufferHandle handle{};
        RGBufferRange  range{};
    };

    BufferInput                    frame{};
    BufferInput                    light{};
    RGBufferHandle                 skinning{};
    std::array<RGTextureHandle, 4> gBufferColors{};
    RGTextureHandle                gBufferDepth{};
    Rect2D                         renderArea{};
    uint32_t                       layerCount = 1;

    // Current-flight binding (owned by DeferredFrameResourceSet); carried so the
    // execute callback can drive the stage without reading stage members.
    DescriptorSetHandle            frameAndLightDescriptorSet{};
    DescriptorSetHandle            skinningDescriptorSet{};
};

/// Typed pass parameters for the Deferred SSAO graph pass.
///
/// Same object drives graph setup (declaration) and execute (resolve + binding);
/// the pass resolves its GBuffer inputs from the graph instead of reading
/// resolved-image snapshots back from the stage.
struct DeferredSSAOPassParams
{
    RGBufferHandle     frame{};
    RGBufferRange      frameRange{};
    RGTextureHandle    albedo{};
    RGTextureHandle    normal{};
    RGTextureHandle    depth{};
    RGTextureHandle    output{};
    DescriptorSetHandle frameDescriptorSet{};
};

/// Typed pass parameters for the Deferred Light graph pass.
///
/// Same object drives graph setup (declaration) and execute (resolve + binding);
/// GBuffer/SSAO descriptor inputs are updated from resolved graph textures
/// inside the pass, removing the resolved-image back-injection into LightStage.
struct DeferredLightPassParams
{
    struct BufferInput
    {
        RGBufferHandle handle{};
        RGBufferRange  range{};
    };

    BufferInput                    frame{};
    BufferInput                    light{};
    std::array<RGTextureHandle, 4> gBufferColors{};
    RGTextureHandle                gBufferDepth{};
    std::optional<RGTextureHandle> ssao{};
    std::optional<RGTextureHandle> environmentCubemap{};
    std::optional<RGTextureHandle> environmentIrradiance{};
    std::optional<RGTextureHandle> environmentPrefilter{};
    std::optional<RGTextureHandle> environmentBrdfLut{};
    std::optional<RGTextureHandle> shadowDepth{};
    RGTextureHandle                viewportColor{};
    Rect2D                         renderArea{};
    uint32_t                       layerCount = 1;
    DescriptorSetHandle            frameAndLightDescriptorSet{};
    DescriptorSetHandle            environmentLightingDescriptorSet{};
};

/// Typed pass parameters for the Deferred skybox graph pass.
///
/// The pass declaration and execute path both consume this object, so execute
/// no longer queries skybox inputs back from ViewportOverlayStage state.
struct DeferredSkyboxPassParams
{
    struct BufferInput
    {
        RGBufferHandle handle{};
        RGBufferRange  range{};
    };

    BufferInput                                   frame{};
    RGTextureHandle                               viewportColor{};
    RGTextureHandle                               depth{};
    Rect2D                                        renderArea{};
    uint32_t                                      layerCount = 1;
    ViewportOverlayStage::FrameInputs::SkyboxInput skybox{};
};

/// Typed pass parameters for the Deferred forward-opaque placeholder graph pass.
struct DeferredForwardOpaquePassParams
{
    RGTextureHandle color{};
    RGTextureHandle depth{};
    Rect2D          renderArea{};
    uint32_t        layerCount = 1;
};

/// Typed pass parameters for the Deferred forward-transparent graph pass.
///
/// The overlay snapshot that was prepared during frame extraction is carried
/// directly into the graph execute callback, removing the need to read it back
/// from ViewportOverlayStage-owned mutable state.
struct DeferredForwardTransparentPassParams
{
    RGTextureHandle                    color{};
    RGTextureHandle                    depth{};
    Rect2D                             renderArea{};
    uint32_t                           layerCount = 1;
    ViewportOverlayStage::FrameInputs  overlay{};
};

/// Typed pass parameters for the Deferred final overlay graph pass.
///
/// The pass receives an explicit callback input instead of capturing the
/// pipeline member directly inside the graph lambda.
struct DeferredOverlayPassParams
{
    RGTextureHandle                                     color{};
    RGTextureHandle                                     depth{};
    Rect2D                                              renderArea{};
    uint32_t                                            layerCount = 1;
    std::shared_ptr<const RenderViewportOverlaySnapshot> overlaySnapshot = nullptr;
    FrameContext                                         frameCtx{};
};

} // namespace ya
