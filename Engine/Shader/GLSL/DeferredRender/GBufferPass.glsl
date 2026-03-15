#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common/Limits.glsl"

struct VertexOutput{
    vec3 pos;
    vec2 uv;
    vec3 normal;
    // mat3 TBN;
};



// MARK: =======
#ifdef SHADER_STAGE_VERTEX


layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;

layout(location = 0) out VertexOutput OUT;

layout(push_constant) uniform PushConstants {
    mat4 modelMat;
} pc;

void main()
{
    vec4 pos = pc.modelMat * vec4(aPos,1.0);
    OUT.pos = pos.xyz;
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

layout(location =0) in VertexOutput IN;

layout(location =0) out vec3 pos;
layout(location =1) out vec3 normal;
layout(location =2) out vec4 albedoSpecular;


layout(set = 1, binding = 0) uniform sampler2D uTexDiffuse;
layout(set = 1, binding = 1) uniform sampler2D uTexSpecular;

void main()
{
    pos = IN.pos;
    normal = normalize(IN.normal);
    vec3 albedo = texture(uTexDiffuse, IN.uv).rgb;
    float specular = texture(uTexSpecular, IN.uv).r;
    albedoSpecular = vec4(albedo, specular);
}
#endif