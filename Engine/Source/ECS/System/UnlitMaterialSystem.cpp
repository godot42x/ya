#include "UnlitMaterialSystem.h"
#include "Core/Math/Geometry.h"
#include "ECS/Component/CameraComponent.h"


#include "Core/App/App.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Swapchain.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Material/UnlitMaterial.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"



#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Scene/Scene.h"



namespace ya

{

void UnlitMaterialSystem::onInit(IRenderPass *renderPass)
{
    _label = "UnlitMaterialSystem";

    IRender *render = getRender();

    auto _sampleCount = ESampleCount::Sample_1;

    // MARK: layout
    PipelineDesc pipelineLayout{
        .label         = "UnlitMaterialSystem_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(UnlitMaterialSystem::PushConstant),
                .stageFlags = EShaderStage::Vertex,
            },
        },
        .descriptorSetLayouts = {
            DescriptorSetLayout{
                .label    = "UnlitMaterial_FrameDSL",
                .set      = 0,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment,
                    },
                },
            },
            DescriptorSetLayout{
                .label    = "UnlitMaterial_ParamDSL",
                .set      = 1,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },

            },
            DescriptorSetLayout{
                .label    = "UnlitMaterial_ResourceDSL",
                .set      = 2,
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
                },
            },
        },
    };

    _materialFrameUboDSL = IDescriptorSetLayout::create(render, pipelineLayout.descriptorSetLayouts[0]);
    _materialParamDSL    = IDescriptorSetLayout::create(render, pipelineLayout.descriptorSetLayouts[1]);
    _materialResourceDSL = IDescriptorSetLayout::create(render, pipelineLayout.descriptorSetLayouts[2]);



    // Use factory method to create pipeline layout
    std::vector<std::shared_ptr<IDescriptorSetLayout>> dslVec = {_materialFrameUboDSL, _materialParamDSL, _materialResourceDSL};
    _pipelineLayout                                           = IPipelineLayout::create(render, pipelineLayout.label, pipelineLayout.pushConstants, dslVec);


    // MARK: pipeline
    _pipelineDesc = GraphicsPipelineCreateInfo{
        .subPassRef = 0,
        // .pipelineLayout   = pipelineLayout,
        .shaderDesc = ShaderDesc{
            .shaderName        = "Test/Unlit.glsl",
            .bDeriveFromShader = false,
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(ya::Vertex),
                },
            },
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
            },
        },
        // define what state need to dynamically modified in render pass execution
        .dynamicFeatures = EPipelineDynamicFeature::Scissor | // the imgui required this feature as I did not set the dynamical render feature

#if !NOT_DYN_CULL
                           EPipelineDynamicFeature::CullMode |
#endif
                           EPipelineDynamicFeature::Viewport,
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .frontFace   = EFrontFaceType::CounterClockWise, // GL
            // .frontFace = EFrontFaceType::ClockWise, // VK: reverse viewport and front face to adapt vulkan
        },
        .multisampleState = MultisampleState{
            .sampleCount          = _sampleCount,
            .bSampleShadingEnable = false,
        },
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = true,
            .bDepthWriteEnable      = true,
            .depthCompareOp         = ECompareOp::Less,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
            .minDepthBounds         = 0.0f,
            .maxDepthBounds         = 1.0f,
        },
        .colorBlendState = ColorBlendState{
            .attachments = {
                ColorBlendAttachmentState{
                    // 0 is the final present color attachment
                    .index               = 0,
                    .bBlendEnable        = false,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::One,
                    .dstAlphaBlendFactor = EBlendFactor::Zero,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                },
            },
        },
        .viewportState = ViewportState{
            .viewports = {
                {
                    .x        = 0,
                    .y        = 0,
                    .width    = static_cast<float>(render->getSwapchain()->getExtent().width),
                    .height   = static_cast<float>(render->getSwapchain()->getExtent().height),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                },
            },
            .scissors = {Scissor{
                .offsetX = 0,
                .offsetY = 0,
                .width   = render->getSwapchain()->getExtent().width,
                .height  = render->getSwapchain()->getExtent().height,
            }},
        },
    };
    // Use factory method to create graphics pipeline
    _pipeline = IGraphicsPipeline::create(render, renderPass, _pipelineLayout.get());
    // _pipeline      = _pipelineOwner;
    _pipeline->recreate(_pipelineDesc);


    // frames descriptor set pool
    DescriptorPoolCreateInfo poolCI{
        .maxSets   = 1,
        .poolSizes = {
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::UniformBuffer,
                .descriptorCount = 1,
            },
        },
    };
    _frameDSP = IDescriptorPool::create(render, poolCI);
    std::vector<ya::DescriptorSetHandle> sets;
    _frameDSP->allocateDescriptorSets(_materialFrameUboDSL, 1, sets);
    _frameDS = sets[0];

    // recreate use material's descriptor set pool
    recreateMaterialDescPool(NUM_MATERIAL_BATCH);

    _frameUBO = ya::IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = "Unlit_Frame_UBO",
            .usage         = ya::EBufferUsage::UniformBuffer,
            .size          = sizeof(FrameUBO),
            .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,

        });
}

void UnlitMaterialSystem::onRender(ICommandBuffer *cmdBuf, IRenderTarget *rt)
{
    Scene *scene = getActiveScene();
    if (!scene) {
        return;
    }
    auto view = scene->getRegistry().view<TagComponent, UnlitMaterialComponent, TransformComponent>();
    if (view.begin() == view.end()) {
        return;
    }

    // auto cmdBuffer = VulkanCommandBuffer::fromHandle(cmdBuf);
    cmdBuf->bindPipeline(_pipeline.get());

    uint32_t width  = rt->getFrameBuffer()->getWidth();
    uint32_t height = rt->getFrameBuffer()->getHeight();

    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(height);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(height);
        viewportHeight = -static_cast<float>(height);
    }

    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(width), viewportHeight, 0.0f, 1.0f);
    cmdBuf->setScissor(0, 0, width, height);
#if !NOT_DYN_CULL
    cmdBuf->setCullMode(_cullMode);
#endif

    updateFrameDS(rt);

    //
    bool     bShouldForceUpdateMaterial = false;
    uint32_t materialCount              = MaterialFactory::get()->getMaterialSize<UnlitMaterial>();
    if (materialCount > _lastMaterialDSCount) {
        recreateMaterialDescPool(materialCount);
        bShouldForceUpdateMaterial = true;
    }

    std::vector<bool> updatedMaterial(materialCount);

    for (entt::entity entity : view)
    {
        const auto &[tag, umc, tc] = view.get(entity);
        for (const auto &[material, meshIDs] : umc.getMaterial2MeshIDs()) {

            _ctxEntityDebugStr = std::format("{} (Mat: {})", tag._tag, material->getLabel());
            if (!material || material->getIndex() < 0) {
                YA_CORE_WARN("default material for none or error material");
                continue;
            }

            // update each material instance's descriptor set if dirty
            uint32_t            materialInstanceIndex = material->getIndex();
            DescriptorSetHandle paramDS               = _materialParamDSs[materialInstanceIndex];
            DescriptorSetHandle resourceDS            = _materialResourceDSs[materialInstanceIndex];

            // update the resource set when:
            // 1. this has updated, multiple entity using the same material(not material instance?)
            // 2. material count changed
            // 3. this material's resources(such as texture) changed(dirty)
            if (!updatedMaterial[materialInstanceIndex]) {
                if (bShouldForceUpdateMaterial || material->isParamDirty())
                {
                    updateMaterialParamDS(paramDS, material);
                    material->setParamDirty(false);
                }
                {}
                if (bShouldForceUpdateMaterial || material->isResourceDirty())
                {
                    updateMaterialResourceDS(resourceDS, material);
                    material->setResourceDirty(false);
                }
                updatedMaterial[materialInstanceIndex] = true;
            }

            // bind descriptor set
            std::vector<DescriptorSetHandle> descSets{
                _frameDS,
                _materialParamDSs[materialInstanceIndex],
                _materialResourceDSs[materialInstanceIndex],
            };
            cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descSets);

            // update push constant
            UnlitMaterialSystem::PushConstant pushConst{
                .modelMatrix = tc.getTransform(),
            };
            cmdBuf->pushConstants(_pipelineLayout.get(),
                                  EShaderStage::Vertex,
                                  0,
                                  sizeof(UnlitMaterialSystem::PushConstant),
                                  &pushConst);

            // draw each mesh
            for (const auto &meshIndex : meshIDs)
            {
                auto mesh = umc.getMesh(meshIndex);
                if (mesh) {
                    mesh->draw(cmdBuf);
                }
            }
        }
    }
}

void UnlitMaterialSystem::recreateMaterialDescPool(uint32_t _materialCount)
{
    auto *render = getRender();
    YA_CORE_ASSERT(render != nullptr, "Render is null");
    // 1. calculate how many set needed
    uint32_t newDescriptorSetCount = std::max<uint32_t>(1, _lastMaterialDSCount);
    if (_lastMaterialDSCount == 0) {
        _lastMaterialDSCount = newDescriptorSetCount;
    }

    while (newDescriptorSetCount < _materialCount) {
        newDescriptorSetCount *= 2;
    }
    if (newDescriptorSetCount > NUM_MATERIAL_BATCH_MAX) {
        YA_CORE_ASSERT(false, "Too many material, exceed the max limit");
        return;
    }

    // 2. destroy old
    _materialParamDSs.clear();
    _materialResourceDSs.clear();

    // 3. recreate pool
    if (_materialDSP) {
        _materialDSP->resetPool();
    }
    DescriptorPoolCreateInfo poolCI{
        .maxSets   = newDescriptorSetCount * 2, // max(param , resource)
        .poolSizes = {
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::UniformBuffer,
                .descriptorCount = newDescriptorSetCount,
            },
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::CombinedImageSampler,
                .descriptorCount = newDescriptorSetCount * 2, // tex0 + tex1 for each material param in one set
            },
        },
    };
    _materialDSP = IDescriptorPool::create(render, poolCI);


    // 4. allocate new sets
    // 为每一个单独的材质分配描述符集
    _materialDSP->allocateDescriptorSets(_materialParamDSL, newDescriptorSetCount, _materialParamDSs);
    _materialDSP->allocateDescriptorSets(_materialResourceDSL, newDescriptorSetCount, _materialResourceDSs);

    // 5. create UBOs
    uint32_t diffCount = newDescriptorSetCount - _materialParamsUBOs.size();
    for (uint32_t i = 0; i < diffCount; i++) {
        auto buffer = ya::IBuffer::create(
            render,
            ya::BufferCreateInfo{
                .label         = "UnlitMaterial_Param_UBO",
                .usage         = ya::EBufferUsage::UniformBuffer,
                .size          = sizeof(UnlitMaterial::MaterialUBO),
                .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,
            });
        _materialParamsUBOs.push_back(buffer);
    }


    _lastMaterialDSCount = newDescriptorSetCount;
}

// TODO: descriptor set can be shared if they use same layout and data
void UnlitMaterialSystem::updateFrameDS(IRenderTarget *rt)
{
    auto app    = getApp();
    auto render = getRender();

    // Use cached camera context (updated once per frame in App::onUpdate)
    const auto &camCtx = rt->getCameraContext();

    FrameUBO ubo{
        .projection = camCtx.projection,
        .view       = camCtx.view,
        .resolution = {
            rt->getFrameBuffer()->getWidth(),
            rt->getFrameBuffer()->getHeight(),
        },
        .frameIndex = app->getFrameIndex(),
        .time       = (float)app->getElapsedTimeMS() / 1000.0f,
    };

    _frameUBO->writeData(&ubo, sizeof(ubo), 0);

    DescriptorBufferInfo bufferInfo(BufferHandle(_frameUBO->getHandle()), 0, sizeof(FrameUBO));

    auto *descriptorHelper = render->getDescriptorHelper();
    descriptorHelper->updateDescriptorSets(
        {
            IDescriptorSetHelper::genBufferWrite(
                _frameDS,
                0,
                0,
                EPipelineDescriptorType::UniformBuffer,
                {bufferInfo}),
        },
        {});
}

void UnlitMaterialSystem::updateMaterialParamDS(DescriptorSetHandle ds, UnlitMaterial *material)
{
    auto render = getRender();

    YA_CORE_ASSERT(ds.ptr != nullptr, "descriptor set is null: {}", _ctxEntityDebugStr);


    auto &params = material->uMaterial;
    // update param from texture
    const TextureView *tv0 = material->getTextureView(UnlitMaterial::BaseColor0);
    if (tv0) {
        params.textureParam0.updateByTextureView(tv0);
    }
    const TextureView *tv1 = material->getTextureView(UnlitMaterial::BaseColor1);
    if (tv1) {
        params.textureParam1.updateByTextureView(tv1);
    }

    auto paramUBO = _materialParamsUBOs[material->getIndex()].get();
    paramUBO->writeData(&params, sizeof(UnlitMaterial::MaterialUBO), 0);

    DescriptorBufferInfo bufferInfo(BufferHandle(paramUBO->getHandle()), 0, sizeof(UnlitMaterial::MaterialUBO));

    render
        ->getDescriptorHelper()
        ->updateDescriptorSets(
            {
                IDescriptorSetHelper::genBufferWrite(
                    ds,
                    0,
                    0,
                    EPipelineDescriptorType::UniformBuffer,
                    {bufferInfo}),
            },
            {});
}

void UnlitMaterialSystem::updateMaterialResourceDS(DescriptorSetHandle ds, UnlitMaterial *material)
{
    auto render = getRender();


    YA_CORE_ASSERT(ds.ptr != nullptr, "descriptor set is null: {}", _ctxEntityDebugStr);
    // TODO: not texture and default texture?
    // update param from texture
    const TextureView *tv0 = material->getTextureView(UnlitMaterial::BaseColor0);
    const TextureView *tv1 = material->getTextureView(UnlitMaterial::BaseColor1);

    DescriptorImageInfo imageInfo0(SamplerHandle(tv0->sampler->getHandle()),
                                   tv0->texture->getImageViewHandle(),
                                   EImageLayout::ShaderReadOnlyOptimal);
    DescriptorImageInfo imageInfo1(SamplerHandle(tv1->sampler->getHandle()),
                                   tv1->texture->getImageViewHandle(),
                                   EImageLayout::ShaderReadOnlyOptimal);

    render
        ->getDescriptorHelper()
        ->updateDescriptorSets(
            {
                IDescriptorSetHelper::genImageWrite(ds, 0, 0, EPipelineDescriptorType::CombinedImageSampler, {imageInfo0}),
                IDescriptorSetHelper::genImageWrite(ds, 1, 0, EPipelineDescriptorType::CombinedImageSampler, {imageInfo1}),
            },
            {});
}

} // namespace ya
