#include "DeferredRenderPath_PBR.h"

#include "DeferredRenderPipeline.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Render/Material/MaterialFactory.h"
#include "Resource/PrimitiveMeshCache.h"
#include "Resource/TextureLibrary.h"
#include "Scene/Scene.h"

namespace ya
{

void DeferredPBRRenderPath::init(DeferredRenderPipeline& pipeline)
{
    auto* render = pipeline._render;

    auto commonDSLs             = IDescriptorSetLayout::create(render, _commonDescriptorSetLayouts);
    _frameAndLightDSL           = commonDSLs[0];
    _resourceOrLightTexturesDSL = commonDSLs[1];
    _paramsDSL                  = commonDSLs[2];

    _gBufferPPL = IPipelineLayout::create(
        render,
        "Deferred_PBR_GBuffer_PPL",
        {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(DeferredRenderPipeline::GBufferPushConstant),
                .stageFlags = EShaderStage::Vertex,
            },
        },
        commonDSLs);

    GraphicsPipelineCreateInfo gBufferPipelineCI{
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = "Deferred PBR GBuffer Pass",
            .colorAttachmentFormats = {pipeline.COLOR_FORMAT, pipeline.COLOR_FORMAT, pipeline.COLOR_FORMAT},
            .depthAttachmentFormat  = pipeline.DEPTH_FORMAT,
        },
        .pipelineLayout = _gBufferPPL.get(),
        .shaderDesc     = ShaderDesc{
                .shaderName        = "DeferredRender/GBufferPass.slang",
                .bDeriveFromShader = false,
                .vertexBufferDescs = {
                VertexBufferDescription{
                        .slot  = 0,
                        .pitch = sizeof(ya::Vertex),
                },
            },
                .vertexAttributes = _commonVertexAttributes,
        },
        .dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
        },
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .cullMode  = ECullMode::Back,
            .frontFace = EFrontFaceType::CounterClockWise,
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable  = true,
            .bDepthWriteEnable = true,
            .depthCompareOp    = ECompareOp::Less,
        },
        .colorBlendState = ColorBlendState{
            .attachments = {
                ColorBlendAttachmentState{
                    .index               = 0,
                    .bBlendEnable        = false,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,
                    .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                },
                ColorBlendAttachmentState{
                    .index               = 1,
                    .bBlendEnable        = false,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,
                    .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                },
                ColorBlendAttachmentState{
                    .index               = 2,
                    .bBlendEnable        = false,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,
                    .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                },
            },
        },
        .viewportState = ViewportState{
            .viewports = {Viewport::defaults()},
            .scissors  = {Scissor::defaults()},
        },
    };
    _gBufferPipeline = IGraphicsPipeline::create(render);
    YA_CORE_ASSERT(_gBufferPipeline && _gBufferPipeline->recreate(gBufferPipelineCI), "Failed to create deferred PBR GBuffer pipeline");

    const uint32_t texCount = static_cast<uint32_t>(_resourceOrLightTexturesDSL->getLayoutInfo().bindings.size());
    _matPool.init(
        render,
        _paramsDSL,
        _resourceOrLightTexturesDSL,
        [texCount](uint32_t n) {
            return std::vector<DescriptorPoolSize>{
                {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
                {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * texCount},
            };
        },
        16);

    _lightPPL = IPipelineLayout::create(
        render,
        "Deferred_PBR_Light_PPL",
        {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(DeferredRenderPipeline::LightPassPushConstant),
                .stageFlags = EShaderStage::Vertex,
            },
        },
        commonDSLs);

    GraphicsPipelineCreateInfo lightPipelineCI{
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = "Deferred PBR Light Pass",
            .colorAttachmentFormats = {pipeline.COLOR_FORMAT},
            .depthAttachmentFormat  = pipeline.DEPTH_FORMAT,
        },
        .pipelineLayout = _lightPPL.get(),
        .shaderDesc     = ShaderDesc{
                .shaderName        = "DeferredRender/LightPass.slang",
                .bDeriveFromShader = false,
                .vertexBufferDescs = {
                VertexBufferDescription{
                        .slot  = 0,
                        .pitch = sizeof(ya::Vertex),
                },
            },
                .vertexAttributes = _commonVertexAttributes,
        },
        .dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
        },
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .cullMode  = ECullMode::None,
            .frontFace = EFrontFaceType::CounterClockWise,
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable  = true,
            .bDepthWriteEnable = false,
            .depthCompareOp    = ECompareOp::Less,
        },
        .colorBlendState = ColorBlendState{
            .attachments = {
                ColorBlendAttachmentState{
                    .index               = 0,
                    .bBlendEnable        = true,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,
                    .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                },
            },
        },
        .viewportState = ViewportState{
            .viewports = {Viewport::defaults()},
            .scissors  = {Scissor::defaults()},
        },
    };
    _lightPipeline = IGraphicsPipeline::create(render);
    YA_CORE_ASSERT(_lightPipeline && _lightPipeline->recreate(lightPipelineCI), "Failed to create deferred PBR light pipeline");

    _deferredDSP = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .label     = "Deferred_PBR_DSP",
            .maxSets   = 2,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 2,
                },
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 3,
                },
            },
        });

    _frameAndLightDS = _deferredDSP->allocateDescriptorSets(_frameAndLightDSL);
    _lightTexturesDS = _deferredDSP->allocateDescriptorSets(_resourceOrLightTexturesDSL);

    _frameUBO = IBuffer::create(
        render,
        BufferCreateInfo{
            .label         = "Deferred_PBR_Frame_UBO",
            .usage         = EBufferUsage::UniformBuffer,
            .size          = sizeof(DeferredRenderPipeline::LightPassFrameData),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
        });

    _lightUBO = IBuffer::create(
        render,
        BufferCreateInfo{
            .label         = "Deferred_PBR_Light_UBO",
            .usage         = EBufferUsage::UniformBuffer,
            .size          = sizeof(DeferredRenderPipeline::LightPassLightData),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
        });

    _frameUBO->writeData(&_lightPassFrameData, sizeof(_lightPassFrameData), 0);
    _frameUBO->flush();
    _lightUBO->writeData(&_lightPassLightData, sizeof(_lightPassLightData), 0);
    _lightUBO->flush();

    render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneUniformBuffer(_frameAndLightDS, 0, _frameUBO.get()),
        IDescriptorSetHelper::writeOneUniformBuffer(_frameAndLightDS, 1, _lightUBO.get()),
    });
}

void DeferredPBRRenderPath::shutdown(DeferredRenderPipeline& pipeline)
{
    (void)pipeline;
    _lightUBO.reset();
    _frameUBO.reset();
    _deferredDSP.reset();
    _lightPipeline.reset();
    _lightPPL.reset();
    _gBufferPipeline.reset();
    _gBufferPPL.reset();
    _paramsDSL.reset();
    _resourceOrLightTexturesDSL.reset();
    _frameAndLightDSL.reset();
}

void DeferredPBRRenderPath::beginFrame(DeferredRenderPipeline& pipeline)
{
    (void)pipeline;
    _gBufferPipeline->beginFrame();
    _lightPipeline->beginFrame();
}

void DeferredPBRRenderPath::tick(DeferredRenderPipeline& pipeline, const DeferredRenderTickDesc& desc, Scene* scene)
{
    YA_CORE_ASSERT(scene, "DeferredPBRRenderPath requires a valid scene");

    auto drawListView = scene->getRegistry().view<MeshComponent, TransformComponent, PhongMaterialComponent>();

    uint32_t         materialCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<PhongMaterial>());
    bool             force         = _matPool.ensureCapacity(materialCount);
    std::vector<int> preparedMaterial(materialCount, 0);

    for (const auto& [entity, mc, tc, pmc] : drawListView.each()) {
        (void)entity;
        (void)mc;
        (void)tc;

        PhongMaterial* material = pmc.getMaterial();
        if (!material || material->getIndex() < 0) {
            continue;
        }
        uint32_t idx = static_cast<uint32_t>(material->getIndex());
        if (preparedMaterial[idx]) {
            continue;
        }

        _matPool.flushDirty(
            material,
            force,
            [](IBuffer* ubo, PhongMaterial* mat) {
                ParamsData  params{};
                const auto& src = mat->getParams().textureParams;
                for (int i = 0; i < PhongMaterial::EResource::Count; ++i) {
                    params.textures[i].bEnable     = src[i].bEnable;
                    params.textures[i].uvTransform = src[i].uvTransform;
                }
                ubo->writeData(&params, sizeof(ParamsData), 0);
            },
            [&pipeline](DescriptorSetHandle ds, PhongMaterial* mat) {
                pipeline._render->getDescriptorHelper()->updateDescriptorSets(
                    {
                        IDescriptorSetHelper::writeOneImage(ds, 0, mat->getTextureBinding(PhongMaterial::EResource::DiffuseTexture)),
                        IDescriptorSetHelper::writeOneImage(ds, 1, mat->getTextureBinding(PhongMaterial::EResource::SpecularTexture)),
                        IDescriptorSetHelper::writeOneImage(ds, 2, mat->getTextureBinding(PhongMaterial::EResource::NormalTexture)),
                    },
                    {});
            });

        preparedMaterial[idx] = 1;
    }

    for (const auto& [et, dlc, tc] :
         scene->getRegistry().view<DirectionalLightComponent, TransformComponent>().each())
    {
        (void)et;
        _lightPassLightData.dirLight.dir     = tc.getForward();
        _lightPassLightData.dirLight.color   = dlc._color;
        _lightPassLightData.dirLight.ambient = dlc._ambient;
    }

    _lightPassFrameData.viewPos    = desc.cameraPos;
    _lightPassFrameData.projMatrix = desc.projection;
    _lightPassFrameData.viewMatrix = desc.view;
    _frameUBO->writeData(&_lightPassFrameData, sizeof(_lightPassFrameData), 0);
    _frameUBO->flush();

    _lightPassLightData.dirLight.shininess = 32;
    _lightUBO->writeData(&_lightPassLightData, sizeof(_lightPassLightData), 0);
    _lightUBO->flush();

    const uint32_t viewportWidth  = static_cast<uint32_t>(desc.viewportRect.extent.x);
    const uint32_t viewportHeight = static_cast<uint32_t>(desc.viewportRect.extent.y);

    auto cmdBuf = desc.cmdBuf;

    RenderingInfo gBufferRI{
        .label      = "Deferred PBR GBuffer Pass",
        .renderArea = Rect2D{
            .pos    = {0, 0},
            .extent = pipeline._gBufferRT->getExtent().toVec2(),
        },
        .layerCount       = 1,
        .colorClearValues = {
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
        },
        .depthClearValue = ClearValue(1.0f, 0),
        .renderTarget    = pipeline._gBufferRT.get(),
    };
    cmdBuf->beginRendering(gBufferRI);

    cmdBuf->bindPipeline(_gBufferPipeline.get());

    float gBufferViewportY      = 0.0f;
    float gBufferViewportHeight = static_cast<float>(viewportHeight);
    if (pipeline._bReverseViewportY) {
        gBufferViewportY      = static_cast<float>(viewportHeight);
        gBufferViewportHeight = -static_cast<float>(viewportHeight);
    }
    cmdBuf->setViewport(0.0f, gBufferViewportY, static_cast<float>(viewportWidth), gBufferViewportHeight);
    cmdBuf->setScissor(0, 0, viewportWidth, viewportHeight);

    for (const auto& [entity, mc, tc, pmc] : drawListView.each()) {
        (void)entity;
        PhongMaterial* material = pmc.getMaterial();
        if (!material || material->getIndex() < 0) {
            continue;
        }

        uint32_t idx = static_cast<uint32_t>(material->getIndex());
        cmdBuf->bindDescriptorSets(
            _gBufferPPL.get(),
            0,
            {
                _frameAndLightDS,
                _matPool.resourceDS(idx),
                _matPool.paramDS(idx),
            });

        DeferredRenderPipeline::GBufferPushConstant pc{
            .modelMat = tc.getTransform(),
        };
        cmdBuf->pushConstants(
            _gBufferPPL.get(),
            EShaderStage::Vertex,
            0,
            sizeof(DeferredRenderPipeline::GBufferPushConstant),
            &pc);
        mc.getMesh()->draw(cmdBuf);
    }

    cmdBuf->endRendering(gBufferRI);

    pipeline._sharedDepthSpec = RenderingInfo::ImageSpec{
        .texture       = pipeline._gBufferRT->getCurFrameBuffer()->getDepthTexture(),
        .initialLayout = EImageLayout::Undefined,
        .finalLayout   = EImageLayout::Undefined,
    };

    RenderingInfo lightPassRI{
        .label      = "Deferred PBR Light Pass",
        .renderArea = {
            .pos    = {0, 0},
            .extent = pipeline._viewportRT->getExtent().toVec2(),
        },
        .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 0.0f)},
        .colorAttachments = {
            RenderingInfo::ImageSpec{
                .texture       = pipeline._viewportRT->getCurFrameBuffer()->getColorTexture(0),
                .initialLayout = EImageLayout::ColorAttachmentOptimal,
                .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
            },
        },
        .depthAttachment = &pipeline._sharedDepthSpec,
    };

    cmdBuf->beginRendering(lightPassRI);

    auto sampler = TextureLibrary::get().getDefaultSampler();
    pipeline._render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::writeOneImage(
                _lightTexturesDS,
                0,
                pipeline._gBufferRT->getCurFrameBuffer()->getColorTexture(0)->getImageView(),
                sampler.get()),
            IDescriptorSetHelper::writeOneImage(
                _lightTexturesDS,
                1,
                pipeline._gBufferRT->getCurFrameBuffer()->getColorTexture(1)->getImageView(),
                sampler.get()),
            IDescriptorSetHelper::writeOneImage(
                _lightTexturesDS,
                2,
                pipeline._gBufferRT->getCurFrameBuffer()->getColorTexture(2)->getImageView(),
                sampler.get()),
        });

    cmdBuf->bindPipeline(_lightPipeline.get());
    cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
    cmdBuf->setScissor(0, 0, viewportWidth, viewportHeight);
    cmdBuf->bindDescriptorSets(
        _lightPPL.get(),
        0,
        {
            _frameAndLightDS,
            _lightTexturesDS,
        });

    auto quad = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Quad);
    quad->draw(cmdBuf);

    if (pipeline._skyboxSystem && pipeline._skyboxSystem->bEnabled) {
        cmdBuf->debugBeginLabel("Deferred Skybox");
        FrameContext skyboxCtx;
        skyboxCtx.view       = desc.view;
        skyboxCtx.projection = desc.projection;
        skyboxCtx.cameraPos  = desc.cameraPos;
        skyboxCtx.extent     = Extent2D{
                .width  = viewportWidth,
                .height = viewportHeight,
        };
        pipeline._skyboxSystem->tick(cmdBuf, desc.dt, &skyboxCtx);
        cmdBuf->debugEndLabel();
    }

    pipeline._viewportRI             = lightPassRI;
    pipeline._lastTickCtx.view       = desc.view;
    pipeline._lastTickCtx.projection = desc.projection;
    pipeline._lastTickCtx.cameraPos  = desc.cameraPos;
    pipeline._lastTickCtx.extent     = Extent2D{
            .width  = viewportWidth,
            .height = viewportHeight,
    };
    pipeline._lastTickDesc = desc;
}

void DeferredPBRRenderPath::renderGUI(DeferredRenderPipeline& pipeline)
{
    (void)pipeline;
    ImGui::TextUnformatted("PBR path scaffold currently mirrors the Phong deferred path.");
    if (_gBufferPipeline) {
        _gBufferPipeline->renderGUI();
    }
    if (_lightPipeline) {
        _lightPipeline->renderGUI();
    }
}

} // namespace ya
