#pragma once

#include "Common/Helper.slang"

inline float radicalInverse_VDC(uint bits)
{
    bits = (bits<<16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // 0x100000000
}

// 获取大小为 n 的样本集的低差异样本 i
inline vec2 hammersley(uint i , uint N)
{
    return vec2( float(i)/float(N), radicalInverse_VDC(i));
}

inline vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness * roughness;

    /* 半球的方位角 yaw, 根据 index 获得(0 ~ 2PI) */
    float phi = 2.0 * PI * Xi.x;

    /* 俯仰角 pitch, 基于 roughness 权重偏移 */
    float cosTheta = sqrt( (1.0 - Xi.y) / (1.0 +(a*a - 1.0) * Xi.y) );
    float sinTheta = sqrt( 1.0 - cosTheta * cosTheta);

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // transform tangent-space vector  to world-space sample vector
    vec3 up = abs(N.z) <0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 T = normalize( cross(up, N) );
    vec3 B = cross(N, T);

    vec3 sampleVec = T * H.x + B *H.y + N* H.z;

    return normalize(sampleVec);
}


inline float G_geometrySchlickGGX(float NdotV, float roughness)
{
    // direct light 
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 -k) +k;
    return  nom / denom;
}

inline float G_geometrySchlickGGX_IBL(float NdotV, float roughness)
{
    // IBL indirect light
    float a = roughness;
    float k = (a* a) / 2.0; 

    float nom = NdotV;
    float denom = NdotV * (1.0 -k) +k;
    return  nom / denom;
}

inline float G_geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = G_geometrySchlickGGX(NdotV, roughness);
    float ggx1 = G_geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

inline float G_geometrySmith_IBL(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = G_geometrySchlickGGX_IBL(NdotV, roughness);
    float ggx1 = G_geometrySchlickGGX_IBL(NdotL, roughness);
    return ggx1 * ggx2;
}