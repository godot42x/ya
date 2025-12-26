#pragma once
#include "Core/Base.h"

#include "Core/System/System.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/RenderDefines.h"


struct VulkanPipelineLayout;
struct VulkanPipeline;
struct VulkanBuffer;


namespace ya
{

struct LitMaterial;

static constexpr uint32_t NUM_MATERIAL_BATCH     = 16;
static constexpr uint32_t NUM_MATERIAL_BATCH_MAX = 2048;



struct LitMaterialSystem : public IMaterialSystem
{

    struct FrameUBO
    {
        glm::mat4 projection{1.f};
        glm::mat4 view{1.f};
        alignas(8) glm::ivec2 resolution;
        alignas(4) uint32_t frameIndex = 0;
        alignas(4) float time;
        alignas(16) glm::vec3 cameraPos; // 相机世界空间位置
    };

    // 点光源结构体（对应 shader 中的 PointLight）
    struct PointLightData
    {
        alignas(16) glm::vec3 position; // 光源位置
        float intensity;                // 光照强度
        alignas(16) glm::vec3 color;    // 光源颜色
        float radius;                   // 光照半径
    };

    static constexpr uint32_t MAX_POINT_LIGHTS = 4;

    // std140 布局规则（GLSL）：
    // - vec3 自身占12字节，但按16字节对齐（下一个变量从16字节边界开始）
    // - scalar (float/uint) 按4字节对齐
    // - 数组元素按最大成员对齐（vec3数组元素按16字节）
    struct alignas(16) LightUBO
    {
        alignas(16) glm::vec3 directionalLightDir = glm::vec3(-0.5f, -1.0f, -0.3f);
        float directionalLightIntensity           = 1.0f; // 可以紧跟在vec3后面的4字节空间

        alignas(16) glm::vec3 directionalLightColor = glm::vec3(1.0f);
        float ambientIntensity                      = 0.1f; // 可以紧跟在vec3后面的4字节空间

        alignas(16) glm::vec3 ambientColor = glm::vec3(1.0f);
        uint32_t numPointLights            = 0;

        // offset 64: 数组（按16对齐）
        alignas(16) PointLightData pointLights[MAX_POINT_LIGHTS];
    } uLight;

    struct DebugUBO
    {
        alignas(4) bool bDebugNormal = false;
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

    std::shared_ptr<IPipelineLayout>   _pipelineLayout;
    std::shared_ptr<IGraphicsPipeline> _pipeline;

    // set 0, contains the frame UBO and lighting UBO
    stdptr<IDescriptorPool> _frameDSP;
    DescriptorSetHandle     _frameDS;
    stdptr<IBuffer>         _frameUBO;
    stdptr<IBuffer>         _lightUBO;
    stdptr<IBuffer>         _debugUBO;



    // material ubo's, dynamically extend
    uint32_t                         _lastMaterialDSCount = 0;
    std::shared_ptr<IDescriptorPool> _materialDSP;

    // object ubo
    std::vector<std::shared_ptr<IBuffer>> _materialParamsUBOs;
    std::vector<DescriptorSetHandle>      _materialParamDSs;    // each material instance
    std::vector<DescriptorSetHandle>      _materialResourceDSs; // each material's texture

    std::string _ctxEntityDebugStr;

    void onInit(IRenderPass *renderPass) override;
    void onDestroy() override;
    void onUpdate(float deltaTime) override;
    void onRender(ICommandBuffer *cmdBuf, IRenderTarget *rt) override;
    void onRenderGUI() override;



  private:
    // void recreateMaterialDescPool(uint32_t count);

    void updateFrameDS(IRenderTarget *rt);
    void updateMaterialParamDS(DescriptorSetHandle ds, LitMaterial *material);
    void updateMaterialResourceDS(DescriptorSetHandle ds, LitMaterial *material);
    void recreateMaterialDescPool(uint32_t _materialCount);
};

} // namespace ya