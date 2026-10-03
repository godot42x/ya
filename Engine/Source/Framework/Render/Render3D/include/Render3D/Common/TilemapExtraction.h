#pragma once

#include "Scene2D/TilemapComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace ya
{

struct TextureBinding;
struct WorldSpriteCandidate;

// Everything appendTilemapCandidates needs besides the component itself.
// textureWidth/Height are the resolved atlas pixels; the atlas TextureSlot
// already resolved them before this runs, so a zero extent only means
// "not ready yet" and yields no candidates.
struct TilemapExtractionInput
{
    const TilemapComponent* map           = nullptr;
    const Tileset*          tileset       = nullptr;
    glm::mat4               world         = glm::mat4(1.0f);
    uint32_t                entityId      = 0;
    const TextureBinding*   atlas         = nullptr;
    uint32_t                textureWidth  = 0;
    uint32_t                textureHeight = 0;
    // Optional inclusive cell range. The scene snapshot passes the whole
    // map; a future view-frustum or chunk cache can narrow it. Empty when
    // min > max after clamping.
    bool    bHasVisibleRange = false;
    int32_t minX = 0;
    int32_t minY = 0;
    int32_t maxX = -1;
    int32_t maxY = -1;
};

// Expands the non-empty cells in range into sprite candidates, one per
// (layer, cell). A tile whose window falls outside the atlas, and a layer
// whose cell count does not match width * height, are skipped: extraction
// never invents pixels for authoring mistakes. Appends to out, so one call
// per tilemap entity accumulates the snapshot.
YA_RENDER_3D_API void appendTilemapCandidates(const TilemapExtractionInput&     in,
                                              std::vector<WorldSpriteCandidate>& out);

} // namespace ya
