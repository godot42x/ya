#pragma once

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

} // namespace ya
