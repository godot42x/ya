#include "Common/Limits.slang"

// ═══════════════════════════════════════════════════════════════════════════
// Shared GPU structures for Point Shadow Indirect Draw
// Must be kept in sync with ShadowTypes.h on the CPU side.
//
// Design (NoBaseInstance):
//   • Per (batch, face) bucket → exactly ONE IndexedIndirectCommand whose
//     instanceCount is atomically incremented by the cull shader.
//   • Visible global instance indices are appended to a flat buffer
//     `visibleInstances[bucketBase + slot]`. The vertex shader uses
//     SV_InstanceID + a push-constant `bucketBase` to fetch its instance.
// ═══════════════════════════════════════════════════════════════════════════

struct ShadowInstanceData
{
    float4x4 worldMatrix;
    float4   boundingSphere; // xyz=center, w=radius
    uint     batchIndex;     // which mesh batch this instance belongs to
    uint     _pad0;
    uint     _pad1;
    uint     _pad2;
};

struct ShadowFaceFrustum
{
    float4 planes[6];
};

struct IndexedIndirectCommand
{
    uint indexCount;
    uint instanceCount; // atomically updated by cull shader
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};
