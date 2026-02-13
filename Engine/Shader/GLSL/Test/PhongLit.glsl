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

layout(set = 0, binding =2, std140) uniform DebugUBO {
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


// MARK: ===========
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

layout(std140) 
struct DirectionalLight {
    vec3  direction;    // 光源方向
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};


layout(std140)
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
    float innerCutoff; // cos(innerAngle)
    float outerCutoff; // cos(outerAngle)
};



#define MAX_POINT_LIGHTS 4

// MARK: frag uniform
layout(set =0, binding =1, std140) uniform LightUBO {
    DirectionalLight dirLight;
    uint numPointLights;   // collect from scene
    PointLight pointLights[MAX_POINT_LIGHTS];
} uLit;


layout(set = 0, binding =2, std140) uniform DebugUBO {
    bool bDebugNormal;
    bool bDebugDepth;
    bool bDebugUV;
    vec4 floatParam;
} uDebug;


layout(set =1, binding = 0) uniform sampler2D uTexDiffuse;
layout(set =1, binding = 1) uniform sampler2D uTexSpecular;
layout(set =1, binding = 2) uniform sampler2D uTexReflection;

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


// MARK: frag i/o
layout(location = 0) in vec3 vPos;
layout(location = 1) in vec2 vTexcoord;
layout(location = 2) in vec3 vNormal;

layout(location = 0) out vec4 fColor;

float calculateSpec(vec3 norm, vec3 lightDir, vec3 viewDir, float shininess)
{
    #if BLING_PHONG_HALFWAY
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(norm, halfwayDir), 0.0), shininess);
    #else
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    #endif
    return spec;
}

vec3 calculateDirLight(DirectionalLight dirLight, vec3 norm, vec3 viewDir ,vec3 diffuseTexColor, vec3 specularTexColor) 
{
    vec3 lightDir = normalize(-dirLight.direction);
    float diff = max(dot(norm, lightDir), 0.0);

    // float reflectDir = reflect(-lightDir, norm);
    float spec =  calculateSpec(norm, lightDir, viewDir, uParams.shininess);

    vec3 ambient = dirLight.ambient *  diffuseTexColor *  uParams.ambient;
    vec3 diffuse = dirLight.diffuse * diff *   diffuseTexColor * uParams.diffuse;
    vec3 specular = dirLight.specular * spec * specularTexColor * uParams.specular;
    return ambient + diffuse + specular;
}

vec3 calculatePointLight(PointLight pointLight, vec3 fragPos,  vec3 norm,  vec3 viewDir ,vec3 diffuseTexColor, vec3 specularTexColor)
{
    vec3 lightDir = normalize(pointLight.position - fragPos);

    float diff = max(dot(norm, lightDir), 0.0);
    // float reflectDir = reflect(-lightDir, norm);
    float spec = calculateSpec(norm, lightDir, viewDir, uParams.shininess);

    vec3 ambient = pointLight.ambient *  diffuseTexColor * uParams.ambient;
    vec3 diffuse = pointLight.diffuse * diff *   diffuseTexColor * uParams.diffuse;
    vec3 specular = pointLight.specular * spec * specularTexColor * uParams.specular;

    // spot light process
    if (pointLight.type == 1){
        // 
        float theta = dot(lightDir, normalize(-pointLight.spotDir));
        float epsilon = pointLight.innerCutoff - pointLight.outerCutoff;
        // see LearnOpenGL 2.5
        /* LearnOpenGL 2.5. Michael Qiao:
            边缘平滑公式可以这样理解:
            x 的范围是 1 - 10 - 12 , 则边缘平滑公式为: clamp(12 - x / (12-10) , 0, 1)
            1. x在1 - 10范围内公式大于1 clamp为1, 对应的就是内光切的聚光范围
            2. x在10 - 12范围内公式小于1 且 12 - x / (12-10) 越来越小,对应的就是外光切的光越来越弱.
            3. 如果x>12,公式小于0, clamp为(0).
            其实就是从内光切到外光切求1到0的插值
        */
        float intensity =   clamp( (theta - pointLight.outerCutoff) / epsilon, 0.0, 1.0);

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
    float distance = length(pointLight.position - fragPos);
    float attenuation = 1.0 / (
        pointLight.constant +
        pointLight.linear * distance +
        pointLight.quadratic * (distance * distance)
    );


    return (ambient + diffuse + specular) * attenuation;
}

bool drawDebugFrag(vec2 pos, vec2 size, vec4 color)
{
    vec2 fragPos = gl_FragCoord.xy;
    if (fragPos.x >= pos.x && fragPos.x <= pos.x + size.x && fragPos.y >= pos.y && fragPos.y <= pos.y + size.y){
        fColor = color;
        return true;
    }
    return false;
}


// MARK: Fragment Main
void main ()
{
    vec3 norm = normalize(vNormal);
    vec3 viewDir = normalize(uFrame.cameraPos - vPos); // from fragment to camera(eye)
    float shininess =  uParams.shininess;

    vec4 debugColor = vec4(uLit.pointLights[0].type, 0, 0, 1);
    if (drawDebugFrag(vec2(100,100), vec2(30,30), debugColor)){
        return;
    }
    
    if(uDebug.bDebugNormal){
        fColor = vec4(norm * 0.5 + 0.5, 1.0);
        return;
    }
    if(uDebug.bDebugDepth){
        const float near = 0.1;
        const float far = 100.0;
        float d = gl_FragCoord.z;
        d = d * 2.0 - 1.0;          // 窗口深度[0,1]→NDC深度[-1,1]
        d = (2.0 * near * far) / ( (far+near) - d * (far-near) ); // 还原线性深度[0.1,100]
        d = (d - near) / (far - near); // 关键：归一化到[0,1]颜色有效范围
        fColor = vec4(vec3(d), 1.0);
        return;
    }
    if(uLit.numPointLights == 0 ){
        fColor = vec4(0,0,1,1);
        return;
    }

    vec2 uv0 = vTexcoord;
    vec2 uv1 = vTexcoord;
    vec2 uv2 = vTexcoord;

#define GET_UV(to, index)  \
    to = (uParams.texParams[index].uvTransform * vec3(vTexcoord, 1.0)).xy;

    GET_UV(uv0, 0);
    GET_UV(uv1, 1);
    GET_UV(uv2, 2);

    // 调试用：手动构建变换矩阵 (与 CPU FMath::build_transform_mat3 一致)
    // vec2 transform = vec2(0,0);
    // vec2 scale = vec2(1, 1);
    // float rotation = radians(90);
    // float c = cos(rotation);
    // float s = sin(rotation);
    // mat3 transformMat = mat3(
    //     scale.x * c,  scale.x * s, 0,      // 列0
    //     -scale.y * s, scale.y * c, 0,      // 列1
    //     transform.x,  transform.y, 1       // 列2
    // );
    // uv0 = (transformMat * vec3(vTexcoord, 1.0)).xy;

    if(uDebug.bDebugUV){
        fColor = vec4(uv0.x, uv0.y, 0, 1);
        return;
    }

    vec4 diffuseTexColor =  vec4(1);
    vec4 specularTexColor = vec4(1);
    if(uParams.texParams[0].bEnable){
        diffuseTexColor =  texture(uTexDiffuse,  uv0);
    }
    if (uParams.texParams[1].bEnable){
        specularTexColor = texture(uTexSpecular,  uv1);
    }

    // TODO: 未来需要拆分透明和不透明材质的shader
    // lib: 尝试在片段着色器中反转镜面光贴图的颜色值，让木头显示镜面高光而钢制边缘不反光（由于钢制边缘中有一些裂缝，边缘仍会显示一些镜面高光，虽然强度会小很多
    // specularTexColor  = vec4(1.0) -specularTexColor;
    // if (diffuseTexColor.a < 0.1){
    //     discard;
    // }
    
    // 累积所有点光源的光照
    vec3 lighting = vec3(0.0);

    lighting += calculateDirLight(uLit.dirLight, norm, viewDir, diffuseTexColor.xyz, specularTexColor.xyz);
    
    // TODO: need this numPointLights? or make a const to do simd optimization?
    for (uint i = 0u; i < uLit.numPointLights && i < MAX_POINT_LIGHTS; ++i) 
    {
        lighting += calculatePointLight(uLit.pointLights[i], vPos, norm, viewDir, diffuseTexColor.xyz, specularTexColor.xyz);
    }


    vec4 reflectionColor = vec4(0);
    if (uParams.texParams[2].bEnable)
    {
        vec3 I = normalize(-viewDir);
        vec3 R = reflect(I, norm);
        reflectionColor = texture(uSkyBox, R);
        vec4 reflectionRatio = texture(uTexReflection, uv2);
        reflectionColor = reflectionColor * reflectionRatio;
    }

    lighting += reflectionColor.xyz;
    // float a = uDebug.floatParam.x > 0 ? uDebug.floatParam.x : diffuseTexColor.a;
    float a = 1.0f;
    
    fColor = vec4(lighting, a);
}