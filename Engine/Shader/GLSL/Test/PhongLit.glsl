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
    vec4 floatParam;
} uDebug;


layout(location = 0) out vec3 vPos;
layout(location = 1) out vec2 vTexcoord;
layout(location = 2) out vec3 vNormal;

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
    vTexcoord = aTexcoord;
    gl_Position = uFrame.projMat * uFrame.viewMat * pos;
    
    //生成一个 专门用于变换法向量的矩阵，确保法向量在经过模型矩阵的缩放、旋转等操作后，依然垂直于物体表面，从而保证光照计算的正确性。
    mat3 normalMatrix = transpose(inverse(mat3(pc.modelMat)));
    vec3 worldNormal = normalMatrix * aNormal;
    vNormal = worldNormal;
    
}


// MARK: Splitter
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

struct DirectionalLight {
    vec3  direction;    // 光源方向
    float intensity;    // 光照强度
    vec3  color;       // 光源颜色
    float padding;      // 填充以保持对齐
    vec3  ambient;

    // attenuation factors
    float constant;
    float linear;
    float quadratic;
};


struct PointLight {
    int   type; // 0 point, 1 spot
    vec3  position;      // 光源位置
    float intensity;    // 光照强度
    vec3  color;         // 光源颜色
    float radius;       // 光照范围（用于衰减计算）
    vec3  spotDir;
    // float innerAngle;
    // float outerAngle;
    float innerCutoff; // cos(innerAngle)
    float outerCutoff; // cos(outerAngle)
};

#define MAX_POINT_LIGHTS 4

layout(set =0, binding =1, std140) uniform LightUBO {
    DirectionalLight dirLight;

    PointLight pointLights[MAX_POINT_LIGHTS];
    uint numPointLights;   // collect from scene
} uLit;


layout(set = 0, binding =2, std140) uniform DebugUBO {
    bool bDebugNormal;
    vec4 floatParam;
} uDebug;


layout(set =1, binding = 0) uniform sampler2D uTexDiffuse;
layout(set =1, binding = 1) uniform sampler2D uTexSpecular;


layout(set = 2, binding = 0) uniform ParamUBO {
    vec3 ambient; 
    vec3 diffuse;
    vec3 specular;
    float shininess;
} uParams;


layout(location = 0) in vec3 vPos;
layout(location = 1) in vec2 vTexcoord;
layout(location = 2) in vec3 vNormal;

layout(location = 0) out vec4 fColor;

// 计算点光源的衰减
float calculateAttenuation(float distance) {
    // 使用物理衰减：1 / (distance^2)

    float attenuation = 1.0 / (
        uLit.dirLight.constant +
        uLit.dirLight.linear * distance +
        uLit.dirLight.quadratic * (distance * distance)
    );

    return attenuation;

    // float attenuation = 1.0 / (1.0 + distance * distance);
    // // 平滑过渡到边缘
    // float smoothFactor = 1.0 - pow(distance / radius, 4.0);
    // return attenuation * smoothFactor;
}



// MARK: Fragment Main
void main ()
{
    vec3 norm = normalize(vNormal);
    // from fragment to camera(eye)
    vec3 viewDir = normalize(uFrame.cameraPos - vPos);
    float shininess =  uParams.shininess;

    vec3 lightDir = normalize(-uLit.dirLight.direction);
    float ambientIntensity = 0.1;
    
    if(uDebug.bDebugNormal){
        fColor = vec4(norm * 0.5 + 0.5, 1.0);
        return;
    }
    if(uLit.numPointLights == 0){
        fColor = vec4(0,0,1,1);
        return;
    }

    vec4 diffuseTexColor = texture(uTexDiffuse, vTexcoord);
    vec4 specularTexColor = texture(uTexSpecular, vTexcoord);
    // lib: 尝试在片段着色器中反转镜面光贴图的颜色值，让木头显示镜面高光而钢制边缘不反光（由于钢制边缘中有一些裂缝，边缘仍会显示一些镜面高光，虽然强度会小很多
    // specularTexColor  = vec4(1.0) -specularTexColor;
    
    
    
    // 累积所有点光源的光照
    vec3 lighting = vec3(0.0);
    
    for (uint i = 0u; i < uLit.numPointLights && i < MAX_POINT_LIGHTS; ++i) {

        PointLight light = uLit.pointLights[i];

        // 修正：lightDir 应该是从片段指向光源的方向
        vec3 lightDir  = normalize(light.position - vPos);
        float distance = length(light.position - vPos);
        vec3 lampColor = light.color * light.intensity;

     

        // 环境光/ambient（环境光不受距离衰减影响，但强度应该较小）
        vec3 ambient = lampColor * uParams.ambient * diffuseTexColor.rgb * ambientIntensity;
        
        // 漫反射/diffuse
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = lampColor*    (diff * uParams.diffuse) * diffuseTexColor.rgb;
        
        // 高光/specular
    #if BLING_PHONG_HALFWAY
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(norm, halfwayDir), 0.0), shininess);
    #else
        // 修正：使用正确的反射方向（反射入射光线）
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    #endif
        vec3 specular = lampColor * spec * uParams.specular * specularTexColor.rgb;


        // spot light process
        if (light.type == 1){
            // 
            float theta = dot(lightDir, normalize(-light.spotDir));
            float epsilon = light.innerCutoff - light.outerCutoff;
            // see LearnOpenGL 2.5
            /* LearnOpenGL 2.5. Michael Qiao:
             边缘平滑公式可以这样理解:
                x 的范围是 1 - 10 - 12 , 则边缘平滑公式为: clamp(12 - x / (12-10) , 0, 1)
                1. x在1 - 10范围内公式大于1 clamp为1, 对应的就是内光切的聚光范围
                2. x在10 - 12范围内公式小于1 且 12 - x / (12-10) 越来越小,对应的就是外光切的光越来越弱.
                3. 如果x>12,公式小于0, clamp为(0).
                其实就是从内光切到外光切求1到0的插值
            */
            float intensity =   clamp( (theta - light.outerCutoff) / epsilon, 0.0, 1.0);

            //  这样不求边缘的衰减
            // innerCutOff = cos(innerConeAngle)
            // fag->light 在 spotDir 的投影 长度 大于 innerCutoff 
            // if (theta > light.innerCutoff){
            //     // do normal calculation
            // }else{
            //     // only ambient
            //     lighting += lampColor * uParams.ambient * diffuseTexColor.rgb * ambientIntensity;
            //     continue;
            // }

            diffuse *= intensity;
            specular *= intensity;
        }

        // 计算衰减
        float attenuation = calculateAttenuation(distance/*, light.radius*/);
        ambient*= attenuation;
        diffuse*= attenuation;
        specular*= attenuation;
        
        
        // 累加光照
        lighting += (ambient + diffuse + specular);
    }
    
    // 限制输出范围，避免过曝导致的视觉错误
    fColor = vec4(clamp(lighting, 0.0, 1.0), 1.0);
}