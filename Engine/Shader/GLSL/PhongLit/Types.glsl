struct DirectionalLight {
    vec3 direction;    // 光源方向
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    mat4 viewProjection; // 用于阴影映射
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

    mat4 viewProjection; // 用于阴影映射

    // spot light
    vec3  spotDir;
    // float innerAngle;
    // float outerAngle;
    float innerCutoff; // cos(innerAngle)
    float outerCutoff; // cos(outerAngle)
};

#ifndef MAX_POINT_LIGHTS
#define MAX_POINT_LIGHTS 6
#endif



layout(set =0, binding =0, std140) uniform FrameUBO {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    uint frameIdx;
    float time;
    vec3 cameraPos;  // 相机世界空间位置
} uFrame;




layout(set =0, binding =1, std140) uniform LightUBO {
    DirectionalLight dirLight;
    uint numPointLights;   // collect from scene
    PointLight pointLights[MAX_POINT_LIGHTS];
    uint hasDirectionalLight; // TODO: use macro to cut branch
} uLit;

layout(set = 0, binding =2, std140) uniform DebugUBO {
    bool bDebugNormal;
    bool bDebugDepth;
    bool bDebugUV;
    vec4 floatParam;
} uDebug;
