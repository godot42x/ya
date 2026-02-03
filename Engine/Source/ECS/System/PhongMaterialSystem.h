#pragma once
#include "Core/Base.h"

#include "Core/System/System.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/PhongMaterial.h"
#include "Render/RenderDefines.h"


struct VulkanPipelineLayout;
struct VulkanPipeline;
struct VulkanBuffer;


namespace ya
{

struct PhongMaterial;

static constexpr uint32_t NUM_MATERIAL_BATCH     = 16;
static constexpr uint32_t NUM_MATERIAL_BATCH_MAX = 2048;



struct PhongMaterialSystem : public IMaterialSystem
{

    using material_param_t = PhongMaterial::ParamUBO;

    struct FrameUBO
    {
        glm::mat4 projection{1.f};
        glm::mat4 view{1.f};
        alignas(8) glm::ivec2 resolution;
        alignas(4) uint32_t frameIndex = 0;
        alignas(4) float time;
        alignas(16) glm::vec3 cameraPos; // 相机世界空间位置
    };

    struct alignas(16) DirectionalLightData
    {
        alignas(16) glm::vec3 direction = glm::vec3(-0.5f, -1.0f, -0.3f);
        alignas(16) glm::vec3 ambient   = glm::vec3(97 / 256.0);
        alignas(16) glm::vec3 diffuse   = glm::vec3(122 / 256.0);
        alignas(16) glm::vec3 specular  = glm::vec3(31 / 256.0);
    };


    struct alignas(16) PointLightData
    {
        float type = 0;
        // attenuation factors
        float constant  = 1.0f;
        float linear    = 0.09f;
        float quadratic = 0.032f;

        alignas(16) glm::vec3 position;

        alignas(16) glm::vec3 ambient  = glm::vec3(0.1f);
        alignas(16) glm::vec3 diffuse  = glm::vec3(0.5f);
        alignas(16) glm::vec3 specular = glm::vec3(1.0f);

        // spot light
        alignas(16) glm::vec3 spotDir;
        float innerCutOff;
        float outerCutOff;
    };

    static constexpr uint32_t MAX_POINT_LIGHTS = 4;


    // std140 布局规则（GLSL）：
    // - vec3 自身占12字节，但按16字节对齐（下一个变量从16字节边界开始）
    // - scalar (float/uint) 按4字节对齐
    // - 数组元素按最大成员对齐（vec3数组元素按16字节）
    struct alignas(16) LightUBO
    {
        alignas(16) DirectionalLightData dirLight;
        alignas(16) uint32_t numPointLights = 0;
        alignas(16) PointLightData pointLights[MAX_POINT_LIGHTS];
    } uLight;

    struct DebugUBO
    {
        alignas(4) bool bDebugNormal = false;
        alignas(4) bool bDebugDepth  = false;
        glm::vec4 floatParam         = glm::vec4(0.0f);
    } uDebug;



    struct ModelPushConstant
    {
        glm::mat4 modelMat;
    };


    GraphicsPipelineCreateInfo _pipelineDesc;

    std::shared_ptr<IDescriptorSetLayout> _materialFrameDSL;    // set 0: perframe
    std::shared_ptr<IDescriptorSetLayout> _materialResourceDSL; // set 1: per material resource (textures)
    std::shared_ptr<IDescriptorSetLayout> _materialParamDSL;    // set 2: per material param

    std::shared_ptr<IPipelineLayout> _pipelineLayout;
    // std::shared_ptr<IGraphicsPipeline> _pipeline; // temp move to IMaterialSystem

    // set 0, contains the frame UBO and lighting UBO
    stdptr<IDescriptorPool> _frameDSP;
    DescriptorSetHandle     _frameDS;
    stdptr<IBuffer>         _frameUBO;
    stdptr<IBuffer>         _lightUBO;
    stdptr<IBuffer>         _debugUBO;



    // material ubo's, dynamically extend
    uint32_t                         _lastMaterialDSCount        = 0;
    bool                             _bShouldForceUpdateMaterial = false;
    std::shared_ptr<IDescriptorPool> _materialDSP;

    // object ubo
    std::vector<std::shared_ptr<IBuffer>> _materialParamsUBOs;
    std::vector<DescriptorSetHandle>      _materialParamDSs;    // each material instance
    std::vector<DescriptorSetHandle>      _materialResourceDSs; // each material's texture

    std::string _ctxEntityDebugStr;

    EPolygonMode::T _polygonMode = EPolygonMode::Fill; // Polygon rendering mode (Fill, Line, Point)

    void onInit(IRenderPass *renderPass) override;
    void onDestroy() override;
    void onUpdateByRenderTarget(float deltaTime, FrameContext *ctx) override;
    void onRender(ICommandBuffer *cmdBuf, FrameContext *ctx) override;
    void onRenderGUI() override;


  private:
    // void recreateMaterialDescPool(uint32_t count);

    void updateFrameDS(FrameContext *ctx);
    void updateMaterialParamDS(DescriptorSetHandle ds, PhongMaterial *material);
    void updateMaterialResourceDS(DescriptorSetHandle ds, PhongMaterial *material);


    void recreateMaterialDescPool(uint32_t _materialCount);

    DescriptorImageInfo getDescriptorImageInfo(TextureView const *tv0);
};



} // namespace ya

// 存在额外的字段，导致不符合 std140 布局规则, 或者占用了别的数据
YA_REFLECT_BEGIN_EXTERNAL(ya::PhongMaterialSystem::DirectionalLightData)
YA_REFLECT_FIELD(direction)
YA_REFLECT_FIELD(ambient, .color())
YA_REFLECT_FIELD(diffuse, .color())
YA_REFLECT_FIELD(specular, .color())
YA_REFLECT_END_EXTERNAL()