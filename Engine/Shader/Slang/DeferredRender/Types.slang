#pragma once
#include "Common/Helper.slang"


public struct TextureParams{
    bool bEnable;
    mat3 uvTransform;
};


struct ParamsData{
    TextureParams textures[ETextureSlot::Count];
};

struct FrameData{
    float3 viewPos;
    mat4  viewMatrix;
    mat4  projMatrix;
};

struct DirectionalLight{
    vec3 dir;
    vec3 color;
    vec3 ambient;
    float shininess;
};

struct LightData{
    DirectionalLight dirLight;
};


[[vk::binding(0,0)]] ConstantBuffer<FrameData,Std140DataLayout> uFrame;
[[vk::binding(1,0)]] ConstantBuffer<LightData,Std140DataLayout> uLight;

[[vk::binding(0,1)]] Sampler2D uTexGPos;
[[vk::binding(1,1)]] Sampler2D uTexGNormal;
[[vk::binding(2,1)]] Sampler2D uTexGAlbedoSpecular;

[[vk::binding(0, 1)]]  Sampler2D  uTexDiffuse;
[[vk::binding(1, 1)]]  Sampler2D  uTexSpecular;
[[vk::binding(2, 1)]]  Sampler2D  uTexNormal;

[[vk::binding(0, 2)]] ConstantBuffer<ParamsData,Std140DataLayout> uParams;