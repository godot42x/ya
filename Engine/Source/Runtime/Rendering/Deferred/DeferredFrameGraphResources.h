#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/RenderGraph.h"

#include <array>
#include <optional>

namespace ya
{

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
    } textures{};

    struct Passes
    {
        std::optional<RGPassHandle> shadow{};
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

} // namespace ya
