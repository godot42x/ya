#pragma once

#include "Graph/RenderGraph.h"
#include "Render3D/Common/ViewGraphName.h"

#include <cstdint>
#include <string_view>

namespace ya
{

/// Registry identity for a View-owned persistent texture.
///
/// Export and pass names must stay unique within one family graph. Persistent
/// keys identify the executor registry across graphs; two Views must not share
/// `ForwardView.Color`. ViewId 0 still uses `.view0` so a missing task
/// cannot revive an unkeyed global name.
[[nodiscard]] inline RGPersistentTextureKey makeViewPersistentTextureKey(
    std::string_view base,
    uint64_t         viewId)
{
    return RGPersistentTextureKey{.value = makeViewGraphName(base, viewId)};
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
