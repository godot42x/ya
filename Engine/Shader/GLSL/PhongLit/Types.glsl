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
    float farPlane; // TODO: use one farPlane for each point light

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


layout(set =1, binding = 0) uniform sampler2D uTexDiffuse;
layout(set =1, binding = 1) uniform sampler2D uTexSpecular;
layout(set =1, binding = 2) uniform sampler2D uTexReflection;
layout(set =1, binding = 3) uniform sampler2D uTexNormal;

// struct TextureParam{
//     bool bEnable;
//     float uvRotation;
//     vec4  uvTransform; // x,y: scale, z,w: translate
// };

struct TextureParam{
    bool bEnable;
    mat3 uvTransform;
};

layout(set = 2, binding = 0) uniform ParamUBO {
    vec3 ambient; 
    vec3 diffuse;
    vec3 specular;
    float shininess;

    TextureParam texParams[3];
} uParams;

layout(set =3, binding = 0) uniform samplerCube uSkyBox;

layout(set =4, binding = 0) uniform sampler2D uDirectionalLightShadowMap;
// layout(set =4, binding = 1) uniform samplerCubeArray uPointLightShadowMapArray;
layout(set =4, binding = 1) uniform samplerCube uPointLightShadowMapArray[MAX_POINT_LIGHTS];


struct VertexOutput{
    vec3 pos ;
    vec2 uv  ;
    vec3 normal ;
    vec3 tangent ;
    vec4 posInDirLightSpace ;
    mat3 TBN ;
};

struct GeometryOutput{
    vec3 pos ;
    vec2 uv  ;
    vec3 normal ;
    vec4 posInDirLightSpace;
    // mat3 TBN : location = 4;
};