#include "Common/Limits.slang"

struct PointShadowFaceData
{
    float4x4 viewProj;
    float3   lightPos;
    float    farPlane;
};
