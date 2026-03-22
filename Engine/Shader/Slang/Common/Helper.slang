
module Helper;

public typealias mat3 = float3x3;
public typealias mat4 = float4x4;
public typealias vec2 = float2;
public typealias vec3 = float3;
public typealias vec4 = float4;


public float3x3 inverse3x3(float3x3 m)
{
    float3 c0 = m[0], c1 = m[1], c2 = m[2];
    float3 t0 = cross(c1, c2);
    float3 t1 = cross(c2, c0);
    float3 t2 = cross(c0, c1);
    float det = dot(c0, t0);
    float invDet = 1.0 / det;
    // Transpose of cofactor matrix / determinant
    return float3x3(
        float3(t0.x, t1.x, t2.x) * invDet,
        float3(t0.y, t1.y, t2.y) * invDet,
        float3(t0.z, t1.z, t2.z) * invDet
    );
}

// typedef matrix<float,4,4> float4x4;
// support 
public inline float4 operator*(float4x4 mat, float4 vec)
{
    return mul(mat, vec);
}
public inline float3 operator*(float3x3 mat, float3 vec)
{
    return mul(mat, vec);
}

enum  ETextureSlot{
    Diffuse = 0,
    Specular,
    Reflection,
    Normal,
    Count
};