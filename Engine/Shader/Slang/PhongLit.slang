// PhongLit.slang
// Slang translation of Engine/Shader/GLSL/PhongLit/PhongLit.glsl
// Contains vertex, geometry and fragment stages for Phong / Blinn-Phong lighting.

// ============================================================
// Shared type definitions (previously in Types.slang)
// These structs must match the C++ side layout (std140).
// A C++ header is auto-generated from this file by slang_gen_header.py.
// ============================================================

#include "Common/Limits.slang"


struct DirectionalLight
{
    float3 direction;
    float3 ambient;
    float3 diffuse;
    float3 specular;
    float4x4 directionalLightMatrix;
};

struct PointLight
{
    float type;       // 0 = point, 1 = spot
    float constant;
    float linear;
    float quadratic;

    float3 position;

    float3 ambient;
    float3 diffuse;
    float3 specular;

    float3 spotDir;
    float  innerCutOff;  // cos(innerAngle)
    float  outerCutOff;  // cos(outerAngle)
};

struct FrameData
{
    float4x4 projMat;
    float4x4 viewMat;
    int2     resolution;
    uint     frameIdx;
    float    time;
    float3   cameraPos;
};

struct LightData
{
    DirectionalLight dirLight;
    PointLight       pointLights[MAX_POINT_LIGHTS];
    uint             numPointLights;
    uint             hasDirectionalLight;
};

struct DebugData
{
    uint   bDebugNormal;
    uint   bDebugDepth;
    uint   bDebugUV;
    float4 floatParam;
};

// ============================================================
// Shared bindings (mirroring GLSL layout)
// ============================================================

// set=0 : Frame-level uniforms
[[vk::binding(0, 0)]] ConstantBuffer<FrameData,Std140DataLayout>  uFrame;
[[vk::binding(1, 0)]] ConstantBuffer<LightData,Std140DataLayout>  uLit;
[[vk::binding(2, 0)]] ConstantBuffer<DebugData,Std140DataLayout>  uDebug;

// set=1 : Material textures (combined image samplers, matching GLSL sampler2D)
[[vk::binding(0, 1)]]  Sampler2D    uTexDiffuse;
[[vk::binding(1, 1)]]  Sampler2D    uTexSpecular;
[[vk::binding(2, 1)]]  Sampler2D    uTexReflection;

// set=2 : Material parameters
struct TextureParam
{
    uint  bEnable;
    float3x3 uvTransform;
};


struct ParamUBO
{
    float3 ambient;
    float3 diffuse;
    float3 specular;
    float  shininess;

    TextureParam texParams[3];
};

[[vk::binding(0, 2)]] ConstantBuffer<ParamUBO> uParams;

// set=3 : Environment map (combined image sampler)
[[vk::binding(0, 3)]]  TextureCube  uSkyBox;
[[vk::binding(0, 3)]]  SamplerState sSkyBox;

// set=4 : Shadow maps (combined image sampler)
[[vk::binding(0, 4)]]  Sampler2D    uDirectionalLightShadowMap;

// Push constants
struct PushConstants
{
    float4x4 modelMat;
};

[[vk::push_constant]] PushConstants pc;

// ============================================================
// Math helpers (Slang does not provide a built-in matrix inverse)
// ============================================================
float3x3 inverse3x3(float3x3 m)
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

// ============================================================
// Vertex / Fragment inter-stage IO
// ============================================================
struct VSOutput
{
    float3 worldPos            : WorldPos;
    float2 texcoord            : Texcoord;
    float3 normal              : Normal;
    float4 fragLightSpacePos   : LightSpacePos;
};

// ============================================================
// MARK: Vertex Shader
// ============================================================
struct VSInput
{
    [[vk::location(0)]] float3 aPos      : POSITION;
    [[vk::location(1)]] float2 aTexcoord : TEXCOORD0;
    [[vk::location(2)]] float3 aNormal   : NORMAL;
};

[shader("vertex")]
VSOutput vertMain(VSInput input, out float4 sv_position : SV_Position)
{
    float4 pos = mul(pc.modelMat, float4(input.aPos, 1.0));

    VSOutput output;
    output.worldPos  = pos.xyz;
    output.texcoord  = input.aTexcoord;
    output.fragLightSpacePos = float4(0.0, 0.0, 0.0, 0.0);

    sv_position = mul(uFrame.projMat, mul(uFrame.viewMat, pos));

    // Normal matrix = transpose(inverse(mat3(modelMat)))
    float3x3 modelMat3 = float3x3(
        pc.modelMat[0].xyz,
        pc.modelMat[1].xyz,
        pc.modelMat[2].xyz
    );
    float3x3 normalMatrix = transpose(inverse3x3(modelMat3));
    output.normal = mul(normalMatrix, input.aNormal);

    // Directional light space transform
    if (uLit.hasDirectionalLight == 1)
    {
        output.fragLightSpacePos = mul(uLit.dirLight.directionalLightMatrix, pos);
    }

    return output;
}

// ============================================================
// MARK: Geometry Shader
// ============================================================
struct GSInput
{
    float3 worldPos          : WorldPos;
    float2 texcoord          : Texcoord;
    float3 normal            : Normal;
    float4 fragLightSpacePos : LightSpacePos;
    float4 sv_position       : SV_Position;
};

struct GSOutput
{
    float3 worldPos          : WorldPos;
    float2 texcoord          : Texcoord;
    float3 normal            : Normal;
    float4 fragLightSpacePos : LightSpacePos;
    float4 sv_position       : SV_Position;
};

[shader("geometry")]
[maxvertexcount(3)]
[instance(1)]
void geomMain(triangle GSInput input[3], inout TriangleStream<GSOutput> outStream)
{
    // Default pass-through (explode effect is commented out in original GLSL)
    for (int i = 0; i < 3; i++)
    {
        GSOutput o;
        o.sv_position       = input[i].sv_position;
        o.worldPos          = input[i].worldPos;
        o.texcoord          = input[i].texcoord;
        o.normal            = input[i].normal;
        o.fragLightSpacePos = input[i].fragLightSpacePos;
        outStream.Append(o);
    }
    outStream.RestartStrip();
}

// ============================================================
// MARK: Fragment Shader helpers
// ============================================================

float calculateSpec(float3 norm, float3 lightDir, float3 viewDir, float shininess)
{
    // Blinn-Phong halfway vector
    float3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), shininess);
    return spec;
}

float calculateDirectionalShadow(float4 fragLightSpacePos, float3 norm, float3 lightDir)
{
    float w = fragLightSpacePos.w;
    if (abs(w) < 1e-6)
        return 0.0;

    float3 projCoords = fragLightSpacePos.xyz / w;
    float2 uv = projCoords.xy * 0.5 + 0.5;
    float curDepth = projCoords.z;

    // Tolerate both ZO [0,1] and NO [-1,1] depth conventions
    if (curDepth < 0.0 || curDepth > 1.0)
    {
        float curDepth01 = curDepth * 0.5 + 0.5;
        if (curDepth01 >= 0.0 && curDepth01 <= 1.0)
            curDepth = curDepth01;
    }
    if (curDepth <= 0.0 || curDepth >= 1.0)
        return 0.0;

    float bias = max(0.05 * (1.0 - dot(norm, lightDir)), 0.005);

    // Simple shadow (non-PCF)
    float closestDepth = uDirectionalLightShadowMap.Sample( uv).r;
    float shadow = (curDepth - bias) > closestDepth ? 1.0 : 0.0;
    return shadow;
}

float3 calculateDirLight(
    DirectionalLight dirLight,
    float3 norm, float3 viewDir,
    float3 diffuseTexColor, float3 specularTexColor)
{
    float3 lightDir = normalize(-dirLight.direction);
    float diff = max(dot(norm, lightDir), 0.0);
    float spec = calculateSpec(norm, lightDir, viewDir, uParams.shininess);

    float3 ambient  = dirLight.ambient  * diffuseTexColor  * uParams.ambient;
    float3 diffuse  = dirLight.diffuse  * diff  * diffuseTexColor  * uParams.diffuse;
    float3 specular = dirLight.specular * spec  * specularTexColor * uParams.specular;

    // Shadow
    // Reuse the interpolated fragLightSpacePos from the fragment input
    // (passed via the global fInput variable set in fragMain)
    float shadow = _fragLightSpacePos_shadow;
    return ambient + (1.0 - shadow) * (diffuse + specular);
}

float3 calculatePointLight(
    PointLight pointLight,
    float3 fragPos, float3 norm, float3 viewDir,
    float3 diffuseTexColor, float3 specularTexColor)
{
    float3 lightDir = normalize(pointLight.position - fragPos);

    float diff = max(dot(norm, lightDir), 0.0);
    float spec = calculateSpec(norm, lightDir, viewDir, uParams.shininess);

    float3 ambient  = pointLight.ambient  * diffuseTexColor  * uParams.ambient;
    float3 diffuse  = pointLight.diffuse  * diff  * diffuseTexColor  * uParams.diffuse;
    float3 specular = pointLight.specular * spec  * specularTexColor * uParams.specular;

    // Spot light
    if (pointLight.type == 1.0)
    {
        float theta   = dot(lightDir, normalize(-pointLight.spotDir));
        float epsilon = pointLight.innerCutOff - pointLight.outerCutOff;
        float intensity = clamp((theta - pointLight.outerCutOff) / epsilon, 0.0, 1.0);
        diffuse  *= intensity;
        specular *= intensity;
    }

    // Attenuation
    float distance    = length(pointLight.position - fragPos);
    float attenuation = 1.0 / (
        pointLight.constant +
        pointLight.linear    * distance +
        pointLight.quadratic * distance * distance
    );

    return (ambient + diffuse + specular) * attenuation;
}

// ============================================================
// MARK: Fragment Shader
// ============================================================
struct FSInput
{
    float3 worldPos          : WorldPos;
    float2 texcoord          : Texcoord;
    float3 normal            : Normal;
    float4 fragLightSpacePos : LightSpacePos;
};

// Module-level variable for shadow calculation (set in fragMain, read in calculateDirLight)
static float _fragLightSpacePos_shadow = 0.0;

[shader("fragment")]
float4 fragMain(FSInput input) : SV_Target
{
    float3 norm    = normalize(input.normal);
    float3 viewDir = normalize(uFrame.cameraPos - input.worldPos);

    // Debug: normals
    if (uDebug.bDebugNormal != 0)
        return float4(norm * 0.5 + 0.5, 1.0);

    // Debug: depth
    if (uDebug.bDebugDepth != 0)
    {
        const float near = 0.1;
        const float far  = 100.0;
        float d = input.fragLightSpacePos.z; // approximate; use SV_Position.z in practice
        d = d * 2.0 - 1.0;
        d = (2.0 * near * far) / ((far + near) - d * (far - near));
        d = (d - near) / (far - near);
        return float4(d, d, d, 1.0);
    }

    // UV transforms
    float2 uv0 = mul(uParams.texParams[0].uvTransform, float3(input.texcoord, 1.0)).xy;
    float2 uv1 = mul(uParams.texParams[1].uvTransform, float3(input.texcoord, 1.0)).xy;
    float2 uv2 = mul(uParams.texParams[2].uvTransform, float3(input.texcoord, 1.0)).xy;

    // Debug: UV
    if (uDebug.bDebugUV != 0)
        return float4(uv0.x, uv0.y, 0.0, 1.0);

    // Sample textures
    float4 diffuseTexColor  = float4(1.0, 1.0, 1.0, 1.0);
    float4 specularTexColor = float4(1.0, 1.0, 1.0, 1.0);

    if (uParams.texParams[0].bEnable != 0)
        diffuseTexColor = uTexDiffuse.Sample( uv0);

    if (uParams.texParams[1].bEnable != 0)
        specularTexColor = uTexSpecular.Sample(uv1);

    // Accumulate lighting
    float3 lighting = float3(0.0, 0.0, 0.0);

    // Pre-calculate directional shadow for the helper function
    if (uLit.hasDirectionalLight == 1)
    {
        float3 dirLightDir = normalize(-uLit.dirLight.direction);
        _fragLightSpacePos_shadow = calculateDirectionalShadow(
            input.fragLightSpacePos, norm, dirLightDir);

        lighting += calculateDirLight(
            uLit.dirLight, norm, viewDir,
            diffuseTexColor.xyz, specularTexColor.xyz);
    }

    for (uint i = 0; i < uLit.numPointLights && i < MAX_POINT_LIGHTS; ++i)
    {
        lighting += calculatePointLight(
            uLit.pointLights[i], input.worldPos, norm, viewDir,
            diffuseTexColor.xyz, specularTexColor.xyz);
    }

    // Reflection (environment map)
    if (uParams.texParams[2].bEnable != 0)
    {
        float3 I = normalize(-viewDir);
        float3 R = reflect(I, norm);
        float4 reflectionColor = uSkyBox.Sample(sSkyBox, R);
        float4 reflectionRatio = uTexReflection.Sample(uv2);
        lighting += (reflectionColor * reflectionRatio).xyz;
    }

    return float4(lighting, 1.0);
}
