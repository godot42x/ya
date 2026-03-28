#pragma once

#include "IDeferredRenderPath.h"

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/PBRMaterial.h"
#include "Render/RenderDefines.h"

#include "DeferredRender.PBR_LightPass.slang.h"
namespace ya
{

struct DeferredPBRRenderPath : IDeferredRenderPath
{
    using ParamUBO = PBRMaterial::ParamUBO;

    stdptr<IDescriptorPool>      _deferredDSP;
    stdptr<IGraphicsPipeline>    _gBufferPipeline;
    stdptr<IPipelineLayout>      _gBufferPPL;
    stdptr<IDescriptorSetLayout> _frameAndLightDSL;
    stdptr<IDescriptorSetLayout> _resourceOrLightTexturesDSL;
    stdptr<IDescriptorSetLayout> _lightGBufferDSL; // 3-binding DSL for light pass GBuffer sampling
    stdptr<IDescriptorSetLayout> _paramsDSL;
    stdptr<IGraphicsPipeline>    _lightPipeline;
    stdptr<IPipelineLayout>      _lightPPL;
    DescriptorSetHandle          _frameAndLightDS = nullptr;
    DescriptorSetHandle          _lightTexturesDS = nullptr;
    stdptr<IBuffer>              _frameUBO        = nullptr;
    stdptr<IBuffer>              _lightUBO        = nullptr;

    using GBufferPassFrameData = slang_types::DeferredRender::PBR_GBufferPass::FrameData;
    using LightPassLightData   = slang_types::DeferredRender::PBR_LightPass::LightData;

    GBufferPassFrameData _gBufferPassFrameData{};
    LightPassLightData   _lightPassLightData{};

    MaterialDescPool<PBRMaterial, ParamUBO> _matPool;

    std::vector<VertexAttribute> _commonVertexAttributes = {
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 0,
            .format     = EVertexAttributeFormat::Float3,
            .offset     = offsetof(ya::Vertex, position),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 1,
            .format     = EVertexAttributeFormat::Float2,
            .offset     = offsetof(ya::Vertex, texCoord0),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 2,
            .format     = EVertexAttributeFormat::Float3,
            .offset     = offsetof(ya::Vertex, normal),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 3,
            .format     = EVertexAttributeFormat::Float3,
            .offset     = offsetof(ya::Vertex, tangent),
        },
    };

    // GBuffer pass descriptor set layouts:
    //   set 0 = Frame+Light UBO
    //   set 1 = PBR material textures (5 bindings)
    //   set 2 = PBR param UBO
    // Light pass uses a separate layout: set 0 (shared) + set 1 (_lightGBufferDSL, 3 GBuffer textures)
    std::vector<DescriptorSetLayoutDesc> _gBufferDescriptorSetLayouts = {
        DescriptorSetLayoutDesc{
            .label    = "Deferred_PBR_Frame_And_Light_DSL",
            .set      = 0,
            .bindings = {
                DescriptorSetLayoutBinding{
                    .binding         = 0,
                    .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
                DescriptorSetLayoutBinding{
                    .binding         = 1,
                    .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
            },
        },
        DescriptorSetLayoutDesc{
            .label    = "Deferred_PBR_MaterialResources_DSL",
            .set      = 1,
            .bindings = {
                // Albedo
                DescriptorSetLayoutBinding{
                    .binding         = 0,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
                // Normal
                DescriptorSetLayoutBinding{
                    .binding         = 1,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
                // Metallic
                DescriptorSetLayoutBinding{
                    .binding         = 2,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
                // Roughness
                DescriptorSetLayoutBinding{
                    .binding         = 3,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
                // AO
                DescriptorSetLayoutBinding{
                    .binding         = 4,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
            },
        },
        DescriptorSetLayoutDesc{
            .label    = "Deferred_PBR_MaterialParams_DSL",
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
    };

    const char* name() const override { return "PBR"; }
    void        init(DeferredRenderPipeline& pipeline) override;
    void        shutdown(DeferredRenderPipeline& pipeline) override;
    void        beginFrame(DeferredRenderPipeline& pipeline) override;
    void        tick(DeferredRenderPipeline& pipeline, const DeferredRenderTickDesc& desc, Scene* scene) override;
    void        renderGUI(DeferredRenderPipeline& pipeline) override;
};

} // namespace ya
