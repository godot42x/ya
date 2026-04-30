#ifndef ENABLE_SKINNING
    #define ENABLE_SKINNING 0
#endif

#ifndef SKINNING_SET_INDEX
    #define SKINNING_SET_INDEX 3
#endif

#if ENABLE_SKINNING
layout(set = SKINNING_SET_INDEX, binding = 0, std430) readonly buffer SkinningPaletteBuffer {
    mat4 uSkinningPalettes[];
};

mat4 getSkinningBoneMatrix(int paletteIndex, int boneIndex)
{
    return uSkinningPalettes[paletteIndex * MAX_BONE_COUNT + boneIndex];
}

void applySkinning(in int paletteIndex,
                   in ivec4 boneIDs,
                   in vec4 weights,
                   inout vec4 localPos,
                   inout vec3 localNormal,
                   inout vec3 localTangent)
{
    mat4 blendedSkinMat = mat4(0.0);
    mat3 blendedNormalSkinMat = mat3(0.0);
    bool hasWeights = false;

    for (int i = 0; i < 4; ++i) {
        int boneID = boneIDs[i];
        float weight = weights[i];
        if (boneID < 0 || weight <= 0.001) {
            continue;
        }

        mat4 boneMat = getSkinningBoneMatrix(paletteIndex, boneID);
        mat3 boneMat3 = mat3(boneMat);
        blendedSkinMat += boneMat * weight;
        blendedNormalSkinMat += transpose(inverse(boneMat3)) * weight;
        hasWeights = true;
    }

    if (hasWeights) {
        localPos = blendedSkinMat * localPos;
        localNormal = normalize(blendedNormalSkinMat * localNormal);
        localTangent = normalize(blendedNormalSkinMat * localTangent);
    }
}
#endif
