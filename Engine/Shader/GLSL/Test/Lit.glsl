#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexcoord;
layout(location = 2) in vec3 aNormal;

layout(set =0, binding =0, std140) uniform FrameUBO {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    uint frameIdx;
    float time;
    vec3 cameraPos;  // 相机世界空间位置
} uFrame;

layout(set = 0, binding =3, std140) uniform DebugUBO {
    bool bDebugNormal;
} uDebug;


layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vNormal;

layout(push_constant) uniform PushConstants{
    mat4 modelMat;
}pc;

#ifndef IS_VULKAN
#define IS_VULKAN 1
#endif

// MARK: Vertex Main
void main (){
    vec4 pos =  pc.modelMat * vec4(aPos, 1.0);
    vPos = pos.xyz;
    gl_Position = uFrame.projMat * uFrame.viewMat * pos;
    
    // 法线变换：使用法线矩阵（模型矩阵的逆转置的3x3部分）
    // mat3 normalMatrix = transpose(inverse(mat3(pc.modelMat)));
    mat4 normalMatrix = transpose(inverse(mat4(pc.modelMat)));
    // vec3 worldNormal = normalMatrix * aNormal;
    vec4 worldNormal = normalMatrix * vec4(aNormal, 0.0);
    vNormal = worldNormal.xyz;
    
}


#type fragment
#version 450



layout(set =0, binding =0, std140) uniform FrameUBO {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    uint frameIdx;
    float time;
    vec3 cameraPos;  // 相机世界空间位置
} uFrame;



// 点光源结构体
struct PointLight {
    vec3  position;      // 光源位置
    float intensity;    // 光照强度
    vec3  color;         // 光源颜色
    float radius;       // 光照范围（用于衰减计算）
};

// 最多支持 4 个点光源
#define MAX_POINT_LIGHTS 4

layout(set =0, binding =1, std140) uniform LightUBO {
    vec3  directionalLightDir;
    float directionalLightIntensity;

    vec3  directionalLightColor;
    float ambientIntensity;

    vec3  ambientColor;
    uint numPointLights;  // 实际使用的点光源数量

    PointLight pointLights[MAX_POINT_LIGHTS];
} uLit;

layout(set = 0, binding =2, std140) uniform DebugUBO {
    bool bDebugNormal;
} uDebug;


layout(set = 1, binding = 0) uniform ParamUBO {
    vec3 objectColor;
} uParams;

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNormal;

layout(location = 0) out vec4 fColor;

// 计算点光源的衰减
float calculateAttenuation(float distance, float radius) {
    return 1;
    // 使用物理衰减：1 / (distance^2)
    // 添加半径限制，超出范围衰减为 0
    if (distance > radius) {
        return 0.0;
    }
    float attenuation = 1.0 / (1.0 + distance * distance);
    // 平滑过渡到边缘
    float smoothFactor = 1.0 - pow(distance / radius, 4.0);
    return attenuation * smoothFactor;
}



// MARK: Fragment Main
void main ()
{
    vec3 norm = normalize(vNormal);
    vec3 viewDir = normalize(uFrame.cameraPos - vPos);
    vec3 objectColor = uParams.objectColor;
    
    if(uDebug.bDebugNormal){
        fColor = vec4(norm * 0.5 + 0.5, 1.0);
        return;
    }
    if(uLit.numPointLights == 0){
        fColor = vec4(0,0,1,1);
        return;
    }
    
    // 环境光
    vec3 ambient = uLit.ambientIntensity * uLit.ambientColor;
    
    // 累积所有点光源的光照
    vec3 lighting = vec3(0.0);
    
    for (uint i = 0u; i < uLit.numPointLights && i < MAX_POINT_LIGHTS; ++i) {

        PointLight light = uLit.pointLights[0];
        
        // 光照方向（从片段指向光源）
        vec3 lightDir = light.position - vPos;
        float distance = length(lightDir);
        lightDir = normalize(lightDir);
        
        // 漫反射
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * light.color * light.intensity;
        
        // 镜面反射（Blinn-Phong）
        // vec3 halfwayDir = normalize(lightDir + viewDir);
        // float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);
        // vec3 specular = spec * light.color * light.intensity;
        
        // 衰减
        float attenuation = 1.0;// calculateAttenuation(distance, light.radius);
        
        // 累加光照
        // lighting += (diffuse + specular) * attenuation;
        lighting +=  diffuse;
        // fColor = vec4(0,1,0,1);
        // break; // 暂时只支持一个点光源
    }

    // lighting = uLit.directionalLightColor;
    
    // 最终颜色
    vec3 color = (ambient + lighting) * objectColor;
    
    // fColor = vec4(0,0,1,1);
    fColor = vec4(color, 1.0);
}