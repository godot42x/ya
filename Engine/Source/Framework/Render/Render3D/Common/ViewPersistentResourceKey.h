#pragma once

#include "Graph/RenderGraph.h"

#include <cstdint>
#include <format>
#include <string_view>

namespace ya
{

/// Registry identity for a View-owned persistent texture.
///
/// Export names stay unique within one graph. Persistent keys identify the
/// executor registry across graphs; two Views must not share `ForwardViewport.Color`.
/// ViewId 0 still uses `.view0` so a missing task cannot revive an unkeyed global name.
[[nodiscard]] inline RGPersistentTextureKey makeViewPersistentTextureKey(
    std::string_view base,
    uint64_t         viewId)
{
    return RGPersistentTextureKey{.value = std::format("{}.view{}", base, viewId)};
}

[[nodiscard]] inline RGTextureHandle createViewPersistentTexture(
    RenderGraph&     graph,
    RGTextureDesc    desc,
    std::string_view base,
    uint64_t         viewId)
{
    const auto key = makeViewPersistentTextureKey(base, viewId);
    desc.label     = key.value;
    return graph.createPersistentTexture(desc, key);
}

} // namespace ya
