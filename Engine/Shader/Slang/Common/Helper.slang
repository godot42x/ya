
// module Helper;
#pragma once
#define public

public typealias mat3 = float3x3;
public typealias mat4 = float4x4;
public typealias vec2 = float2;
public typealias vec3 = float3;
public typealias vec4 = float4;


public inline float3x3 inverse3x3(float3x3  m)
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


// Matrix-vector multiplication overloads
public inline float4 operator*(float4x4 mat, float4 vec) { return mul(mat, vec); }
public inline float3 operator*(float3x3 mat, float3 vec) { return mul(mat, vec); }
public inline float2 operator*(float2x2 mat, float2 vec) { return mul(mat, vec); }

// Matrix-matrix multiplication overloads
public inline float4x4 operator*(float4x4 a, float4x4 b) { return mul(a, b); }
public inline float3x3 operator*(float3x3 a, float3x3 b) { return mul(a, b); }
public inline float2x2 operator*(float2x2 a, float2x2 b) { return mul(a, b); }


public enum  ETextureSlot{
    Diffuse = 0,
    Specular,
    Reflection,
    Normal,
    Count
};

static const float4 RED = float4(1,0,0,1);
static const float4 GREEN = float4(0,1,0,1);
static const float4 BLUE = float4(0,0,1,1);


inline mat4 inverse(in mat4 m)
{
    mat4  ret = mat4(1.0);
    matrix<float,8,4> aug;
    // 初始化增广矩阵：左半部分是原矩阵，右半部分是单位矩阵
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            aug[i][j] = m[i][j];
            aug[i][j + 4] = (i == j) ? 1.0 : 0.0;
        }
    }

    for(int col = 0; col < 4; col++)
    {
        // 寻找主元
        int pivotRow = col;
        float maxVal = abs(aug[col][col]);
        for (int row = col + 1; row < 4; row++)
        {
            float curVal = abs(aug[row][col]);
            if (curVal > maxVal){
                maxVal =  curVal;
                pivotRow = row;
            }
        }

        // 如果主元接近于零，矩阵可能不可逆
        if(maxVal< 1e-6f){
            return ret;
        }

        // 交换当前行和主元行
        if(pivotRow != col){
            for (int j = 0; j < 8; j++)
            {
                float temp = aug[col][j];
                aug[col][j] = aug[pivotRow][j];
                aug[pivotRow][j] = temp;
            }
        }

        // 将主元行归一化
        float div = aug[col][col];
        for(int j = col; j<8; ++j){
            aug[col][j] /= div;
        }

        // 消元
        for(int row = 0; row < 4; ++row){
            if(row != col && abs(aug[row][col]) > 1e-6f){
                float factor = aug[row][col];
                for(int j = col; j < 8; ++j){
                    aug[row][j] -= factor * aug[col][j];
                }
            }
        }

    }
    // 将右半部分提取为逆矩阵
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            ret[i][j] = aug[i][j + 4];
        }
    }
    return ret;
}