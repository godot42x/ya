#include "Common/Helper.slang"

struct VertexInput{
    [[vk::location(0)]] float3 pos      : POSITION;
    [[vk::location(1)]] float2 uv       : TEXCOORD0;
    [[vk::location(2)]] float3 normal   : NORMAL;
    [[vk::location(3)]] float3 tangent  : TANGENT;
};

struct VertexOutput{
    float4 sv_position: SV_Position;
    float3 pos :POSITION;
    float2 uv   :UV;
    float3 normal :NORMAL;
    float3 tangent:  TANGENT;
    // float3x3 TBN;
};

struct PushConstants
{
    float4x4 modelMat;
};

[[vk::push_constant]] PushConstants pc;

// Set 0: Frame + Light UBO (shared with PBR)
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

// Set 1: Phong material textures
[[vk::binding(0, 1)]]  Sampler2D  uTexDiffuse;
[[vk::binding(1, 1)]]  Sampler2D  uTexSpecular;
[[vk::binding(2, 1)]]  Sampler2D  uTexNormal;

// Set 2: Phong material params UBO
public struct TextureParams{
    bool bEnable;
    mat3 uvTransform;
};

struct ParamsData{
    vec3 ambient;
    vec3 specular;
    vec3 diffuse;
    float shininess;
    TextureParams textures[ETextureSlot::Count];
};

[[vk::binding(0, 2)]] ConstantBuffer<ParamsData,Std140DataLayout> uParams;


[shader("vertex")]
void vertMain(in VertexInput IN, out VertexOutput OUT)
{
    float4 worldPos = pc.modelMat * float4(IN.pos, 1.0);
    float4 clipPos = uFrame.projMatrix * uFrame.viewMatrix * worldPos;

    OUT.sv_position = clipPos;
    OUT.pos = worldPos.xyz;
    OUT.uv = IN.uv;

    // TODO: should move to fragment, avoid lerp error
    vec3 N =  normalize((pc.modelMat * vec4(IN.normal, 0)).xyz);
    vec3 T =  normalize((pc.modelMat * vec4(IN.tangent,0)).xyz);
    // vec3 B = cross(N,T);
    // mat3 TBN = transpose(inverse3x3(mat3(T,B,N)));
    // mat3 TBN = transpose(mat3(T,B,N)); // orthographic mat inverse can be ignore

    // float3x3 normalMat = transpose(inverse3x3(float3x3(pc.modelMat)));
    OUT.normal =  N;
    // OUT.TBN = TBN;
    OUT.tangent = T;
}


// MARK: Fragment

struct FragmentOutput{
    float4 posShininess      : SV_Target0;  // (position.xyz, shininess=0.0)
    float4 normalAO          : SV_Target1;  // (normal.xyz,   ao(unused))
    float4 albedoSpecular    : SV_Target2;  // (albedo.rgb(diffuse * diffuseTexture.rgb),  specular)
    float  shadingModelID    : SV_Target3;  // shading model ID (2=Phong, encoded as id/255.0)
};


[shader("fragment")]
void fragMain(in VertexOutput IN, out FragmentOutput OUT)
{
    // Position — roughness unused in Phong, fill 0.0
    OUT.posShininess = vec4(IN.pos, uParams.shininess);

    // Normal — ao unused in Phong, fill 1.0 (no darkening)
    vec3 N = normalize(IN.normal);
    OUT.normalAO.xyz = N;
    if(uParams.textures[ETextureSlot::Normal].bEnable){
        vec3 T = normalize(IN.tangent);
        T = normalize(T - dot(T, N) * N);
        mat3 TBN = mat3(T,cross(N,T),N);
        // vec2 uv = (TBN * vec3(IN.uv,0)).xy;
        vec3 normal = uTexNormal.Sample(IN.uv).xyz;
        // vec3 normal = normalize(uTexNormal.Sample(IN.uv).xyz);
        normal = normalize(normal * 2.0 - 1.0);
        // normal = normalize(normal);
        OUT.normalAO.xyz = normalize(TBN*  normal);
    }

    // Albedo + specular intensity
    float4 albedoSpec;
    albedoSpec.rgb = uParams.diffuse;
    if(uParams.textures[ETextureSlot::Diffuse].bEnable){
        albedoSpec.rgb *=  uTexDiffuse.Sample(IN.uv).rgb;
    }

    float specularMatIntensity = dot(uParams.specular, vec3(0.299, 0.587, 0.114));
    albedoSpec.a = specularMatIntensity;
    
    if(uParams.textures[ETextureSlot::Specular].bEnable){
        albedoSpec.a *= uTexSpecular.Sample(IN.uv).r;
    }
    OUT.albedoSpecular = albedoSpec;
    OUT.shadingModelID = 2.0 / 255.0; // EShadingModelID::Phong = 2
}

// /* TODO
// 核心结论
// TBN 矩阵从顶点着色器到片段着色器的线性插值（lerp）必然会产生失真，核心源于线性插值对曲面非线性旋转的不匹配，以及插值后 TBN 正交归一性的破坏；失真程度可通过规范的资产处理和着色器优化大幅缓解，绝大多数实时渲染场景下可控制在肉眼不可察的范围。
// 一、失真的核心成因
// 1. 线性插值直接破坏 TBN 的正交归一性
// 标准 TBN 矩阵是正交归一矩阵，切线 T、副切线 B、法线 N 三个轴互相垂直且均为单位向量，这是法线空间变换无畸变的核心前提。
// 但从数学上看，两个正交矩阵的线性插值，结果几乎不可能保持正交性。光栅化阶段对 T、B、N 三个向量的分量独立执行线性插值后，会直接导致：
// 向量间夹角改变，失去互相垂直的正交特性
// 向量长度改变，不再是单位向量
// 直接使用插值后的矩阵变换法线，会出现向量偏斜、缩放，最终引发高光异常、明暗扭曲、法线方向反转等视觉失真。
// 2. 线性插值无法匹配曲面 TBN 的非线性变化
// 三角形顶点的 TBN 矩阵，本质是曲面在该点的局部旋转坐标系。对于平滑曲面，三角形内任意一点的正确 TBN，是三个顶点 TBN 的球面非线性插值（对应曲面的连续旋转），而非简单的线性插值。
// 低模面数越少、曲面曲率越大，线性插值带来的方向误差就越明显；极端情况（如 90 度硬边不拆分顶点）会出现完全错误的平滑过渡，导致光照效果完全失真。
// 3. 透视矫正插值的额外误差
// 光栅化阶段对 varying 变量执行的是透视矫正插值，而非简单的线性插值，这会进一步放大 TBN 向量的插值误差，尤其在三角形大角度倾斜于相机时，误差会更加明显。
// 二、加剧失真的典型场景
// 模型面数极低，顶点间距大，单三角面覆盖的曲面曲率变化剧烈
// UV 严重拉伸，导致相邻顶点的切线 T 方向差异巨大
// 硬边 / 平滑组边界未拆分顶点，共享顶点的 TBN 不连续，插值出现断层
// 模型存在非均匀缩放，顶点着色器中未用逆转置矩阵正确变换 T、N 向量
// 法线贴图烘焙与渲染时的 TBN 计算标准不统一（如未使用 MikkTSpace 标准）
// 三、行业通用的失真缓解与修正方案
// 1. 片段着色器内重构正交 TBN（必做，主流引擎标准方案）
// 这是成本最低、收益最高的优化，核心逻辑是不插值完整 TBN 矩阵，仅插值切线 T 和法线 N，在片段着色器中重构严格正交的 TBN：
// 顶点着色器仅输出float4 tangent（w 分量存储副切线手性符号）和float3 normal，减少 varying 传输量的同时，降低插值误差源
// 片段着色器中执行以下步骤：
// 对插值后的 T、N 分别执行归一化
// 用 Gram-Schmidt 正交化修正 T，确保 T 与 N 严格垂直
// 通过cross(N, T) * tangent.w计算副切线 B，保证 TBN 正交且手性正确
// 该方案可彻底解决插值带来的正交性破坏问题，是 Unity、Unreal 等商业引擎的默认实现。
// 2. 资产层面前置优化（从根源减少误差）
// 硬边 / 平滑组边界必须拆分顶点，保证每个三角面的顶点 TBN 连续一致
// UV 布局避免严重拉伸，保证切线方向在三角面内平滑过渡
// 法线贴图烘焙与渲染统一使用 MikkTSpace 标准，确保 TBN 计算完全一致
// 合理增加低模面数，降低单三角面覆盖的曲面曲率变化幅度
// 3. 进阶优化：四元数 SLERP 替代向量 LERP
// 将顶点的 TBN 旋转矩阵转换为单位四元数，对四元数执行球面线性插值（SLERP），再在片段着色器中还原为 TBN 矩阵。
// 该方案完美匹配曲面的连续旋转特性，从根本上解决线性插值的方向误差，尤其适合大曲率曲面、高精度影视级渲染；缺点是四元数插值与转换的计算开销更高，移动端需谨慎使用。
// 4. 极端场景：跳过插值，逐面 / 逐像素计算 TBN
// 对于极致精度需求，可在曲面细分阶段（Tessellation）或几何着色器中，逐三角面 / 逐像素重新计算 TBN，完全跳过插值环节，实现零插值失真。该方案仅适合离线渲染、高端 PC / 主机的特写镜头，通用场景性价比极低。
// 补充说明
// 对于平面、三角面内三个顶点的 TBN 完全一致的场景，插值不会产生任何失真（插值结果与原值完全一致）。
// 常规游戏场景中，只要做好片段着色器的正交重构 + 规范的资产处理，插值失真完全可以忽略，不会影响最终视觉效果。
// 多数新手遇到的法线贴图 “失真”，并非插值本身导致，而是未做正交归一化、手性错误、TBN 空间变换错误、非均匀缩放未处理等基础问题。
// */