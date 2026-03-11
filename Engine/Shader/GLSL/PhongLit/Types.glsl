struct DirectionalLight {
    vec3 direction;    // 光源方向
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    mat4 directionalLightMatrix;
};

struct PointLight {
    // attenuation factors
    float   type; // 0 point, 1 spot
    float constant;
    float linear;
    float quadratic;

    vec3  position;      // 光源位置

    vec3  ambient;
    vec3  diffuse;
    vec3  specular;

    // spot light
    vec3  spotDir;
    // float innerAngle;
    // float outerAngle;
    float innerCutOff; // cos(innerAngle)
    float outerCutOff; // cos(outerAngle)
};

#include "Common/Limits.glsl"



layout(set =0, binding =0, std140) uniform FrameData {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    uint frameIdx;
    float time;
    vec3 cameraPos;  // 相机世界空间位置
} uFrame;




layout(set =0, binding =1, std140) uniform LightData {
    DirectionalLight dirLight;
    PointLight pointLights[MAX_POINT_LIGHTS];
    uint numPointLights;   // collect from scene
    uint hasDirectionalLight; // TODO: use macro to cut branch
} uLit;

layout(set = 0, binding =2, std140) uniform DebugData {
    bool bDebugNormal;
    bool bDebugDepth;
    bool bDebugUV;
    vec4 floatParam;
} uDebug;
