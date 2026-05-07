#include "Common/Limits.slang"

// ═══════════════════════════════════════════════════════════════════════════
// Shared GPU structures for Point Shadow Indirect Draw
// Must be kept in sync with ShadowTypes.h on the CPU side.
// ═══════════════════════════════════════════════════════════════════════════

/// Per-instance data: transform + bounding sphere + mesh draw params.
struct ShadowInstanceData
{
    float4x4 worldMatrix;
    float4   boundingSphere; // xyz=center, w=radius
    uint     indexCount;
    uint     firstIndex;
    int      vertexOffset;
    uint     _pad0;
};

/// Per-face frustum planes for culling (6 planes, xyz=normal, w=d).
struct ShadowFaceFrustum
{
    float4 planes[6];
};

/// VkDrawIndexedIndirectCommand-compatible.
struct IndexedIndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};
