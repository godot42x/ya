#pragma once

#include "Common/Helper.slang"

#ifndef ENABLE_SKINNING
    #define ENABLE_SKINNING 0
#endif

#ifndef SKINNING_SET_INDEX
    #define SKINNING_SET_INDEX 3
#endif

#if ENABLE_SKINNING
    #define YA_SKINNING_VERTEX_FIELDS \
        [[vk::location(4)]] vector<int32_t, MAX_BONE_WEIGHT_PER_VERTEX> boneIDs : BONE_IDS; \
        [[vk::location(5)]] vector<float, MAX_BONE_WEIGHT_PER_VERTEX> weights : WEIGHTS;
#else
    #define YA_SKINNING_VERTEX_FIELDS
#endif

struct SkinningPalette
{
    mat4 boneMatrices[MAX_BONE_COUNT];
};

#if ENABLE_SKINNING
[[vk::binding(0, SKINNING_SET_INDEX)]] StructuredBuffer<SkinningPalette> uSkinningPalettes;
#endif

struct SkinningResult
{
    float4 localPos;
    float3 localNormal;
    float3 localTangent;
};

inline SkinningResult makeDefaultSkinningResult(float3 pos, float3 normal, float3 tangent)
{
    SkinningResult result;
    result.localPos     = float4(pos, 1.0);
    result.localNormal  = normal;
    result.localTangent = tangent;
    return result;
}

inline SkinningResult applySkinning(float3 pos,
                                    float3 normal,
                                    float3 tangent,
                                    int paletteIndex,
                                    vector<int32_t, MAX_BONE_WEIGHT_PER_VERTEX> boneIDs,
                                    vector<float, MAX_BONE_WEIGHT_PER_VERTEX> weights)
{
    SkinningResult result = makeDefaultSkinningResult(pos, normal, tangent);

#if ENABLE_SKINNING
    if (paletteIndex < 0) {
        return result;
    }

    mat4 blendedSkinMat = mat4(0.0);
    mat3 blendedNormalSkinMat = mat3(0.0);
    bool hasWeights = false;

    for (int i = 0; i < MAX_BONE_WEIGHT_PER_VERTEX; ++i) {
        int boneID = boneIDs[i];
        float weight = weights[i];
        if (boneID < 0 || weight <= 0.001) {
            continue;
        }

        mat4 boneMat = uSkinningPalettes[paletteIndex].boneMatrices[boneID];
        mat3 boneMat3 = float3x3(boneMat[0].xyz, boneMat[1].xyz, boneMat[2].xyz);

        blendedSkinMat += boneMat * weight;
        blendedNormalSkinMat += transpose(inverse3x3(boneMat3)) * weight;
        hasWeights = true;
    }

    if (hasWeights) {
        result.localPos = mul(blendedSkinMat, result.localPos);
        result.localNormal = normalize(mul(blendedNormalSkinMat, result.localNormal));
        result.localTangent = normalize(mul(blendedNormalSkinMat, result.localTangent));
    }
#endif

    return result;
}
