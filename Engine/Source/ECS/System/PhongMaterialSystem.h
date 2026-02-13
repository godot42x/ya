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

    static constexpr uint32_t MAX_POINT_LIGHTS = 2 * 3; // reuse times * 3 frame buffer?


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
        alignas(4) bool bDebugUV     = false;
        glm::vec4 floatParam         = glm::vec4(0.0f);
    } uDebug;



    struct ModelPushConstant
    {
        glm::mat4 modelMat;
    };

    PipelineLayoutDesc _pipelineLayoutDesc{
        .label         = "PhongMaterialSystem_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(PhongMaterialSystem::ModelPushConstant),
                .stageFlags = EShaderStage::Vertex | EShaderStage::Geometry,
            },
        },
        .descriptorSetLayouts = {
            // per frame
            DescriptorSetLayoutDesc{
                .label    = "PhongMaterial_Frame_DSL",
                .set      = 0,
                .bindings = {
                    // Frame UBO
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex | EShaderStage::Geometry | EShaderStage::Fragment,
                    },
                    // Lighting
                    DescriptorSetLayoutBinding{
                        .binding         = 1,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                    // Reserved binding = 2
                    DescriptorSetLayoutBinding{
                        .binding         = 2,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment,
                    },
                },
            },
            DescriptorSetLayoutDesc{
                .label    = "PhongMaterial_Resource_DSL",
                .set      = 1,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                    DescriptorSetLayoutBinding{
                        .binding         = 1,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                    // reflection texture
                    DescriptorSetLayoutBinding{
                        .binding         = 2,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
            DescriptorSetLayoutDesc{
                .label    = "PhongMaterial_Param_DSL",
                .set      = 2,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
            DescriptorSetLayoutDesc{
                .label    = "SkyBox_CubeMap_DSL",
                .set      = 3,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
        },
    };


    GraphicsPipelineCreateInfo _pipelineDesc;

    std::shared_ptr<IDescriptorSetLayout> _materialFrameDSL;    // set 0: perframe
    std::shared_ptr<IDescriptorSetLayout> _materialResourceDSL; // set 1: per material resource (textures)
    std::shared_ptr<IDescriptorSetLayout> _materialParamDSL;    // set 2: per material param
    // std::shared_ptr<IDescriptorSetLayout> _skyBoxCbeMapDSL;    // set 3: skyBoxCubeMap

    std::shared_ptr<IPipelineLayout> _pipelineLayout;
    // std::shared_ptr<IGraphicsPipeline> _pipeline; // temp move to IMaterialSystem

    // TODO: Consider using single UBO with dynamic offsets (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
    //       instead of multiple slot buffers for better cache locality and reduced resource count.
    //       Current multi-slot approach: 12 buffers + 4 descriptor sets
    //       Dynamic UBO approach: 3 buffers + 1 descriptor set (with dynamic offsets)
    //       Performance impact: <1% for 2-4 passes, but code simplification benefit is significant.
    //       Blocked by: Would require DSL redesign and descriptor handling refactor.

    // Ring buffer slots for multi-pass rendering (mirror + viewport).
    // When _passSlot >= MAX_PASS_SLOTS, it wraps around: getPassSlot() = _passSlot % MAX_PASS_SLOTS
    // WARNING: If more than MAX_PASS_SLOTS passes reuse this system in one frame,
    //          GPU data hazard may occur if earlier passes haven't finished execution.
    static constexpr uint32_t MAX_PASS_SLOTS = 8;
    uint32_t                  _passSlot      = 0;
    stdptr<IDescriptorPool>   _frameDSP;
    DescriptorSetHandle       _frameDSs[MAX_PASS_SLOTS];
    stdptr<IBuffer>           _frameUBOs[MAX_PASS_SLOTS];
    stdptr<IBuffer>           _lightUBOs[MAX_PASS_SLOTS];
    stdptr<IBuffer>           _debugUBOs[MAX_PASS_SLOTS];

    // TODO: Add GPU event/timeline tracking to detect wrap-around stalls at runtime
    uint32_t getPassSlot() const { return _passSlot % MAX_PASS_SLOTS; }
    void     advanceSlot() { _passSlot = (_passSlot + 1) % MAX_PASS_SLOTS; }



    // material ubo's, dynamically extend
    uint32_t                         _lastMaterialDSCount        = 0;
    bool                             _bShouldForceUpdateMaterial = false;
    std::shared_ptr<IDescriptorPool> _materialDSP;

    // object ubo
    std::vector<std::shared_ptr<IBuffer>> _materialParamsUBOs;
    std::vector<DescriptorSetHandle>      _materialParamDSs;    // each material instance
    std::vector<DescriptorSetHandle>      _materialResourceDSs; // each material's texture

    DescriptorSetHandle skyBoxCubeMapDS = nullptr;
    std::string         _ctxEntityDebugStr;

    EPolygonMode::T _polygonMode = EPolygonMode::Fill; // Polygon rendering mode (Fill, Line, Point)

    // optional?
    void onInit(IRenderPass* renderPass, const PipelineRenderingInfo& pipelineRenderingInfo) override;
    void onDestroy() override;
    void preTick(float deltaTime, FrameContext* ctx);
    void onRender(ICommandBuffer* cmdBuf, FrameContext* ctx) override;
    void onRenderGUI() override;
    void resetFrameSlot() override { _passSlot = 0; }


  private:
    // void recreateMaterialDescPool(uint32_t count);

    void updateFrameDS(FrameContext* ctx);
    void updateMaterialParamDS(DescriptorSetHandle ds, struct PhongMaterialComponent& component, bool bOverrideMirrorMaterial, bool bRecreated);
    void updateMaterialResourceDS(DescriptorSetHandle ds, PhongMaterial* material, bool bOverrideDiffuse);


    void recreateMaterialDescPool(uint32_t _materialCount);

    DescriptorImageInfo getDescriptorImageInfo(IImageView* iv, Sampler* sampler)
    {
        SamplerHandle   samplerHandle   = nullptr;
        ImageViewHandle imageViewHandle = nullptr;
        if (iv) {
            imageViewHandle = iv->getHandle();
        }
        if (sampler) {
            samplerHandle = sampler->getHandle();
        }

        if (imageViewHandle == nullptr) {
            imageViewHandle = TextureLibrary::get().getWhiteTexture()->getImageView()->getHandle();
        }
        if (samplerHandle == nullptr) {
            samplerHandle = TextureLibrary::get().getDefaultSampler()->getHandle();
        }


        DescriptorImageInfo imageInfo0(samplerHandle, imageViewHandle, EImageLayout::ShaderReadOnlyOptimal);
        return imageInfo0;
    }

    DescriptorImageInfo getDescriptorImageInfo(const TextureView* tv)
    {
        if (!tv) {
            return getDescriptorImageInfo(nullptr, nullptr);
        }
        else {
            return getDescriptorImageInfo(tv->texture->getImageView(), tv->sampler);
        }
    }
};


} // namespace ya

// 存在额外的字段，导致不符合 std140 布局规则, 或者占用了别的数据
YA_REFLECT_BEGIN_EXTERNAL(ya::PhongMaterialSystem::DirectionalLightData)
YA_REFLECT_FIELD(direction)
YA_REFLECT_FIELD(ambient, .color())
YA_REFLECT_FIELD(diffuse, .color())
YA_REFLECT_FIELD(specular, .color())
YA_REFLECT_END_EXTERNAL()