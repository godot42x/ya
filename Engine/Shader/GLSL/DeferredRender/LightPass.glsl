#version 450
#extension GL_GOOGLE_include_directive : require

#include "Common/Limits.glsl"

struct FrameData{
    vec3 cameraPos;

};


struct VertexOutput{
    vec2 uv;
};



// MARK: =======
#ifdef SHADER_STAGE_VERTEX


layout(location =0) out vec3 pos;
layout(location =1) out vec3 normal;
layout(location =2) out vec4 albedoSpecular;

layout(location = 0) out VertexOutput OUT;

layout(push_constant) uniform PushConstants {
    mat4 modelMat;
} pc;

void main()
{
    OUT.pos = pc.modelMat * aPos;
    OUT.uv = aUV;

    // vec3 T = normalize(pc.modelMat * vec4(aTangent, 0.0));
    // vec3 N = normalize (pc.modelMat * vec4(aNormal, 0.0));
    // vec3 B = cross(N,T);
    // mat3 TBN = mat3(T, B, N);
    // TBN =  transpose(TBN); // now TBN is the TBN^-1
    // OUT.TBN = TBN;

    // TODO: support normal map texture, by calculate TBN then pass to fragments
    mat3 normalMat = transpose(inverse(mat3(pc.modelMat)));
    OUT.normal =  normalMat * aNormal;
}
#endif


// MARK: ========
#ifdef SHADER_STAGE_FRAGMENT

struct PointLight {
    vec3 pos;
    vec3 color;
};

struct LightData{
    PointLight pointLights[MAX_POINT_LIGHTS_DEFERRED];
    uint numPointLights;
};

layout(set =0, location =0) uniform FrameData uFrame;

layout(set = 1, location = 0) uniform sampler2D uGBufferPos;;
layout(set = 1, location = 1) uniform sampler2D uGBufferNormal;
layout(set = 1, location = 2) uniform sampler2D uGBufferAlbedoSpecular;


layout(location =0) in VertexOutput IN;

layout(location =0) out vec4 fColor;


void main()
{
    vec3 fragPos = texture(uGBufferPos, IN.uv).rgb;
    vec3 normal =  texture(uGBufferNormal, IN.uv).rgb;
    vec4 albedoSpecular = texture(uGBufferAlbedoSpecular, IN.uv);
    vec3 albedo = albedoSpecular.xyz;
    float specular = albedoSpecular.w;

    vec3 lighting = albedo * 0.1;
    vec3 viewDir = normalize(uFrame.cameraPos - fragPos);

    // point light
    for (int i = 0; i < uLit.numPointLights; ++i)
    {
        vec3 lightDir = normalize(uLit.pointLights[i].pos - fragPos);
        float diff = max(dot(normal, lightDir), 0.0);
        lighting += albedo * uLit.pointLights[i].color * diff;

        // Blinn-Phong
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), 16.0) * specular;
        lighting += uLit.pointLights[i].color * spec;
    }
}
#endif