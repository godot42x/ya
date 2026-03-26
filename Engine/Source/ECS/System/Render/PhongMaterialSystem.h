#pragma once
#include "Core/Base.h"

#include "Core/System/System.h"
#include "ECS/System/Render/IMaterialSystem.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/PhongMaterial.h"
#include "Render/RenderDefines.h"
#include <cstddef>

// #include "PhongLit.slang.h"
#include "PhongLit.Types.glsl.h"


struct VulkanPipelineLayout;
struct VulkanPipeline;
struct VulkanBuffer;


namespace ya
{

struct PhongMaterial;

struct PhongScenePassResources
{
    std::string             label;
    stdptr<IDescriptorPool> frameDSP = nullptr;
    DescriptorSetHandle     frameDS  = nullptr;
    stdptr<IBuffer>         frameUBO = nullptr;
    stdptr<IBuffer>         lightUBO = nullptr;
    stdptr<IBuffer>         debugUBO = nullptr;
};

static constexpr uint32_t NUM_MATERIAL_BATCH     = 16;
static constexpr uint32_t NUM_MATERIAL_BATCH_MAX = 2048;



struct PhongMaterialSystem : public IMaterialSystem
{

    using material_param_t = PhongMaterial::ParamUBO;

    YA_DISABLE_PADDED_STRUCT_WARNING_BEGIN()
    // struct FrameUBO
    // {
    //     glm::mat4 projection{1.f};
    //     glm::mat4 view{1.f};
    //     alignas(8) glm::ivec2 resolution;
    //     alignas(4) uint32_t frameIndex = 0;
    //     alignas(4) float time;
    //     alignas(16) glm::vec3 cameraPos; // 相机世界空间位置
    // };

    // using FrameUBO = slang_types::FrameData;
    using FrameUBO = glsl_types::PhongLit::Types::FrameData;

    // struct alignas(16) DirectionalLightData
    // {
    //     alignas(16) glm::vec3 direction = glm::vec3(-0.5f, -1.0f, -0.3f);
    //     alignas(16) glm::vec3 ambient   = glm::vec3(10 / 256.0);
    //     alignas(16) glm::vec3 diffuse   = glm::vec3(30 / 256.0);
    //     alignas(16) glm::vec3 specular  = glm::vec3(31 / 256.0);
    //     alignas(16) glm::mat4 directionalLightMatrix{1.0f}; // directional shadow matrix
    // };

    // using DirectionalLightData = slang_types::DirectionalLight;
    using DirectionalLightData = glsl_types::PhongLit::Types::DirectionalLight;



    // struct alignas(16) PointLightData
    // {
    //     float type = 0;
    //     // attenuation factors
    //     float constant  = 1.0f;
    //     float linear    = 0.09f;
    //     float quadratic = 0.032f;

    //     alignas(16) glm::vec3 position;

    //     alignas(16) glm::vec3 ambient  = glm::vec3(0.1f);
    //     alignas(16) glm::vec3 diffuse  = glm::vec3(0.5f);
    //     alignas(16) glm::vec3 specular = glm::vec3(1.0f);

    //     // spot light
    //     alignas(16) glm::vec3 spotDir;
    //     float innerCutOff;
    //     float outerCutOff;
    // };

    // using PointLightData = slang_types::PointLight;
    using PointLightData = glsl_types::PhongLit::Types::PointLight;



    // std140 布局规则（GLSL）：
    // - vec3 自身占12字节，但按16字节对齐（下一个变量从16字节边界开始）
    // - scalar (float/uint) 按4字节对齐
    // - 数组元素按最大成员对齐（vec3数组元素按16字节）
    // struct alignas(16) LightUBO
    // {
    //     alignas(16) DirectionalLightData dirLight;
    //     alignas(16) PointLightData pointLights[MAX_POINT_LIGHTS];
    //     uint32_t numPointLights      = 0;
    //     uint32_t hasDirectionalLight = 0; // 是否有方向光  TODO: move into the DirectionalLightData, and use it in shader to determine whether to apply directional lighting or not

    // } uLight;

    // using LightUBO = slang_types::LightData;
    using LightUBO = glsl_types::PhongLit::Types::LightData;
    LightUBO uLight;

    // TODO: move to one debug layer system
    //   another pipeline to draw debug effect to the output iamge(or another RT)
    //   redraw ech obj in world? use defer rendering to avoid redraw
    // struct DebugUBO
    // {
    //     uint32_t bDebugNormal            = 0;
    //     uint32_t bDebugDepth             = 0;
    //     uint32_t bDebugUV                = 0;
    //     uint32_t _pad0                   = 0;
    //     alignas(16) glm::vec4 floatParam = glm::vec4(0.0f);
    // } uDebug;

    // using DebugUBO = slang_types::DebugData;
    using DebugUBO = glsl_types::PhongLit::Types::DebugData;
    DebugUBO uDebug;

    struct ModelPushConstant
    {
        glm::mat4 modelMat;
    };
    YA_DISABLE_PADDED_STRUCT_WARNING_END()

    PipelineLayoutDesc _pipelineLayoutDesc{
        .label            = "PhongMaterialSystem_PipelineLayout",
        .vertexAttributes = {
            // (location=0) in vec3 aPos,
            VertexAttribute{
                .bufferSlot = 0,
                .location   = 0,
                .format     = EVertexAttributeFormat::Float3,
                .offset     = offsetof(ya::Vertex, position),
            },
            //  texcoord
            VertexAttribute{
                .bufferSlot = 0, // same buffer slot
                .location   = 1,
                .format     = EVertexAttributeFormat::Float2,
                .offset     = offsetof(ya::Vertex, texCoord0),
            },
            // normal
            VertexAttribute{
                .bufferSlot = 0, // same buffer slot
                .location   = 2,
                .format     = EVertexAttributeFormat::Float3,
                .offset     = offsetof(ya::Vertex, normal),
            },
            // tangent
            VertexAttribute{
                .bufferSlot = 0, // same buffer slot
                .location   = 3,
                .format     = EVertexAttributeFormat::Float3,
                .offset     = offsetof(ya::Vertex, tangent),
            },
        },
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
                        .stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment,
                    },
                    // Debug UBO for now
                    DescriptorSetLayoutBinding{
                        .binding         = 2,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex | EShaderStage::Geometry | EShaderStage::Fragment,
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
                    // normal texture
                    DescriptorSetLayoutBinding{
                        .binding         = 3,
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
                        .stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment,
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
            DescriptorSetLayoutDesc{
                .label    = "ShadowMap_DSL",
                .set      = 4,
                .bindings = {
                    // directional shadow map
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                    // point shadow map
                    DescriptorSetLayoutBinding{
                        .binding         = 1,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = MAX_POINT_LIGHTS,
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

    stdptr<PhongScenePassResources> _currentScenePassResources = nullptr;



    MaterialDescPool<PhongMaterial, PhongMaterial::ParamUBO> _matPool;
    bool                                                      _bDescriptorPoolRecreated = false;

    DescriptorSetHandle depthBufferDS              = nullptr;
    bool                _bDirectionalShadowMapping = true;
    std::string         _ctxEntityDebugStr;

  public:
    PhongMaterialSystem() : IMaterialSystem("PhongMaterialSystem") {}

    // optional?
    stdptr<PhongScenePassResources> createScenePassResources(const std::string& label, const stdptr<IBuffer>& sharedLightUBO = nullptr);

    void setScenePassResources(const stdptr<PhongScenePassResources>& resources);
    void uploadSharedLightUBO(const FrameContext* ctx, IBuffer* sharedLightUBO);
    void onInitImpl(const InitParams& initParams) override;
    void onDestroy() override;
    void preTick(float deltaTime, const FrameContext* ctx);
    void onRender(ICommandBuffer* cmdBuf, const FrameContext* ctx) override;
    void onRenderGUI() override;
    void resetFrameSlot() override { _currentScenePassResources.reset(); }
    void setDirectionalShadowMappingEnabled(bool enabled);


  private:

    void fillLightUBOFromFrameContext(const FrameContext* ctx);
    void updateFrameDS(const FrameContext* ctx);
    void updateMaterialParamUBO(IBuffer* paramUBO, PhongMaterial* material);
    void updateMaterialResourceDS(DescriptorSetHandle ds, PhongMaterial* material);

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


        DescriptorImageInfo imageInfo0(imageViewHandle, samplerHandle, EImageLayout::ShaderReadOnlyOptimal);
        return imageInfo0;
    }

    DescriptorImageInfo getDescriptorImageInfo(const TextureBinding& tb)
    {
        return DescriptorImageInfo(tb.getImageViewHandle(), tb.getSamplerHandle(), EImageLayout::ShaderReadOnlyOptimal);
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