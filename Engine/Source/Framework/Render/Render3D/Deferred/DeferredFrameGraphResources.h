#pragma once

#include "Graph/RenderGraph.h"
#include "Render3D/Common/Shadow/ShadowGraphOutputs.h"

#include <array>
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

} // namespace ya
