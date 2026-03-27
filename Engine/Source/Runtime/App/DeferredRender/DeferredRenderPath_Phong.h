#pragma once

#include "DeferredRenderPipeline.h"
#include "IDeferredRenderPath.h"

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/PhongMaterial.h"
#include "Render/RenderDefines.h"

namespace ya
{

struct DeferredPhongRenderPath : IDeferredRenderPath
{
    using ParamsData = slang_types::DeferredRender::GBufferPass::ParamsData;

    stdptr<IDescriptorPool>       _deferredDSP;
    stdptr<IGraphicsPipeline>     _gBufferPipeline;
    stdptr<IPipelineLayout>       _gBufferPPL;
    stdptr<IDescriptorSetLayout>  _frameAndLightDSL;
    stdptr<IDescriptorSetLayout>  _resourceOrLightTexturesDSL;
    stdptr<IDescriptorSetLayout>  _paramsDSL;
    stdptr<IGraphicsPipeline>     _lightPipeline;
    stdptr<IPipelineLayout>       _lightPPL;
    DescriptorSetHandle           _frameAndLightDS = nullptr;
    DescriptorSetHandle           _lightTexturesDS = nullptr;
    stdptr<IBuffer>               _frameUBO        = nullptr;
    stdptr<IBuffer>               _lightUBO        = nullptr;
    DeferredRenderPipeline::LightPassFrameData _lightPassFrameData{};
    DeferredRenderPipeline::LightPassLightData _lightPassLightData{};
    MaterialDescPool<PhongMaterial, ParamsData> _matPool;

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

    std::vector<DescriptorSetLayoutDesc> _commonDescriptorSetLayouts = {
        DescriptorSetLayoutDesc{
            .label    = "Deferred_GBuffer_Frame_And_Light_DSL",
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
            .label    = "Deferred_GBuffer_MaterialResources_DSL",
            .set      = 1,
            .bindings = {
                DescriptorSetLayoutBinding{
                    .binding         = 0,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
                DescriptorSetLayoutBinding{
                    .binding         = 1,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
                DescriptorSetLayoutBinding{
                    .binding         = 2,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
            },
        },
        DescriptorSetLayoutDesc{
            .label    = "Deferred_GBuffer_MaterialParams_DSL",
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

    const char* name() const override { return "Phong"; }
    void        init(DeferredRenderPipeline& pipeline) override;
    void        shutdown(DeferredRenderPipeline& pipeline) override;
    void        beginFrame(DeferredRenderPipeline& pipeline) override;
    void        tick(DeferredRenderPipeline& pipeline, const DeferredRenderTickDesc& desc, Scene* scene) override;
    void        renderGUI(DeferredRenderPipeline& pipeline) override;
};

} // namespace ya
