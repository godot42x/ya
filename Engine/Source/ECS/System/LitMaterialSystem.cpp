#include "LitMaterialSystem.h"
#include "Core/App/App.h"
#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Swapchain.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Render.h"


#include "Scene/Scene.h"



namespace ya
{

void LitMaterialSystem::onInit(IRenderPass *renderPass)
{
    _label                = "LitMaterialSystem";
    IRender *render       = getRender();
    auto     _sampleCount = ESampleCount::Sample_1;

    // MARK: layout
    PipelineDesc pipelineLayout{
        .label         = "LitMaterialSystem_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(LitMaterialSystem::ModelPushConstant),
                .stageFlags = EShaderStage::Vertex,
            },
        },
        .descriptorSetLayouts = {
            // per frame
            DescriptorSetLayout{
                .label    = "LitMaterial_Frame_DSL",
                .set      = 0,
                .bindings = {
                    // Frame UBO
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment,
                    },
                    // Lighting
                    DescriptorSetLayoutBinding{
                        .binding         = 1,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
            // DescriptorSetLayout{
            //     .label    = "LitMaterial_Resource_DSL",
            //     .set      = 1,
            //     .bindings = {
            //         DescriptorSetLayoutBinding{
            //             .binding         = 0,
            //             .descriptorType  = EPipelineDescriptorType::UniformBuffer,
            //             .descriptorCount = 1,
            //             .stageFlags      = EShaderStage::Fragment,
            //         },
            //     },
            // },
            DescriptorSetLayout{
                .label    = "LitMaterial_Param_DSL",
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
        },
    };

    auto DSLs         = IDescriptorSetLayout::create(render, pipelineLayout.descriptorSetLayouts);
    _materialFrameDSL = DSLs[0];
    // _materialDSL       = DSLs[1];
    _materialParamDSL = DSLs[1];

    // // Use factory method to create pipeline layout
    _pipelineLayout = IPipelineLayout::create(
        render,
        pipelineLayout.label,
        pipelineLayout.pushConstants,
        DSLs);


    // MARK: pipeline
    _pipelineDesc = GraphicsPipelineCreateInfo{
        .subPassRef = 0,
        .shaderDesc = ShaderDesc{
            .shaderName        = "Test/Lit.glsl",
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
                           EPipelineDynamicFeature::Viewport,
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            // .frontFace   = EFrontFaceType::CounterClockWise, // GL
            .frontFace = EFrontFaceType::ClockWise, // VK: reverse viewport and front face to adapt vulkan
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
    _pipeline = IGraphicsPipeline::create(render, renderPass, _pipelineLayout.get());
    _pipeline->recreate(_pipelineDesc);


    _frameDSP = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = 1,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 2, // frame ubo + lighting ubo
                },
            },
        });
    std::vector<ya::DescriptorSetHandle> sets;
    _frameDSP->allocateDescriptorSets(_materialFrameDSL, 1, sets);
    _frameDS = sets[0];

    // TODO: create a auto extend descriptor pool class to support recreate
    recreateMaterialDescPool(NUM_MATERIAL_BATCH);

    _frameUBO = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = "Lit_Frame_UBO",
            .usage         = ya::EBufferUsage::UniformBuffer,
            .size          = sizeof(LitMaterialSystem::FrameUBO),
            .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,

        });
    _lightUBO = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = "Lit_Light_UBO",
            .usage         = ya::EBufferUsage::UniformBuffer,
            .size          = sizeof(LitMaterialSystem::LightUBO),
            .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,
        });
}

void LitMaterialSystem::onDestroy()
{
}

void LitMaterialSystem::onRender(ICommandBuffer *cmdBuf, IRenderTarget *rt)
{
    Scene *scene = getScene();
    if (!scene) {
        return;
    }
    auto view = scene->getRegistry().view<TagComponent, LitMaterialComponent, TransformComponent>();
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

    updateFrameDS(rt);

    bool     bShouldForceUpdateMaterial = false;
    uint32_t materialCount              = MaterialFactory::get()->getMaterialSize<LitMaterial>();
    if (materialCount > _lastMaterialDSCount) {
        recreateMaterialDescPool(materialCount);
        bShouldForceUpdateMaterial = true;
    }

    std::vector<int> updatedMaterial(materialCount, 0);

    for (entt::entity entity : view)
    {
        const auto &[tag, lmc, tc] = view.get(entity);
        for (const auto &[material, meshIDs] : lmc.getMaterial2MeshIDs()) {

            _ctxEntityDebugStr = std::format("{} (Mat: {})", tag.getTag(), material->getLabel());
            if (!material || material->getIndex() < 0) {
                YA_CORE_WARN("default material for none or error material");
                continue;
            }

            // update each material instance's descriptor set if dirty
            uint32_t            materialInstanceIndex = material->getIndex();
            DescriptorSetHandle paramDS               = _materialParamDSs[materialInstanceIndex];
            // DescriptorSetHandle resourceDS = _materialResourceDSs[materialID];
            // DescriptorSetHandle objectDS = _materialObjectDSs[materialIndex];

            // TODO: 拆分更新 descriptor set 和 draw call 为两个循环？ 能否优化效率?

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
                updatedMaterial[materialInstanceIndex] = true;
            }

            // bind descriptor set
            std::vector<DescriptorSetHandle>
                descSets{
                    _frameDS,
                    _materialParamDSs[materialInstanceIndex],
                    // _materialResourceDSs[materialID],
                };
            cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descSets);

            // update push constant
            LitMaterialSystem::ModelPushConstant pushConst{
                .modelMat = tc.getTransform(),
            };
            cmdBuf->pushConstants(_pipelineLayout.get(),
                                  EShaderStage::Vertex,
                                  0,
                                  sizeof(LitMaterialSystem::ModelPushConstant),
                                  &pushConst);

            // draw each mesh
            for (const auto &meshIndex : meshIDs)
            {
                auto mesh = lmc.getMesh(meshIndex);
                if (mesh) {
                    mesh->draw(cmdBuf);
                }
            }
        }
    }
}


// TODO: descriptor set can be shared if they use same layout and data
void LitMaterialSystem::updateFrameDS(IRenderTarget *rt)
{
    auto app    = getApp();
    auto render = getRender();

    glm::mat4 proj;
    glm::mat4 view;
    rt->getViewAndProjMatrix(view, proj);

    FrameUBO uFrame{
        .projection = proj,
        .view       = view,
        .resolution = {
            rt->getFrameBuffer()->getWidth(),
            rt->getFrameBuffer()->getHeight(),
        },
        .frameIndex = app->getFrameIndex(),
        .time       = (float)app->getElapsedTimeMS() / 1000.0f,
    };
    // TODO: grab from scene's light entity/component
    LightUBO uLit{
        .lightDirection   = glm::vec3(-0.5f, -1.0f, -0.3f),
        .lightIntensity   = 1.0f,
        .lightColor       = glm::vec3(1.0f),
        .ambientIntensity = 0.1f,
        .ambientColor     = glm::vec3(0.1f),
    };

    _frameUBO->writeData(&uFrame, sizeof(FrameUBO), 0);
    _lightUBO->writeData(&uLit, sizeof(LightUBO), 0);

    DescriptorBufferInfo frameBufferInfo(_frameUBO->getHandle(), 0, sizeof(FrameUBO));
    DescriptorBufferInfo lightBufferInfo(_lightUBO->getHandle(), 0, sizeof(LightUBO));

    render
        ->getDescriptorHelper()
        ->updateDescriptorSets(
            {
                IDescriptorSetHelper::genBufferWrite(
                    _frameDS,
                    0,
                    0,
                    EPipelineDescriptorType::UniformBuffer,
                    &frameBufferInfo,
                    1),
                IDescriptorSetHelper::genBufferWrite(
                    _frameDS,
                    1,
                    0,
                    EPipelineDescriptorType::UniformBuffer,
                    &lightBufferInfo,
                    1),
            },
            {});
}

void LitMaterialSystem::updateMaterialParamDS(DescriptorSetHandle ds, LitMaterial *material)
{
    auto render = getRender();
    YA_CORE_ASSERT(ds.ptr != nullptr, "descriptor set is null: {}", _ctxEntityDebugStr);


    auto &params = material->getParamsMut();
    // update param from texture
    // const TextureView *tv0 = material->getTextureView(UnlitMaterial::BaseColor0);
    // if (tv0) {
    //     Material::updateTextureParamsByTextureView(tv0, params.textureParam0);
    // }
    // const TextureView *tv1 = material->getTextureView(UnlitMaterial::BaseColor1);
    // if (tv1) {
    //     Material::updateTextureParamsByTextureView(tv1, params.textureParam1);
    // }

    auto paramUBO = _materialParamsUBOs[material->getIndex()];
    paramUBO->writeData(&params, sizeof(LitMaterial::ParamUBO), 0);

    DescriptorBufferInfo bufferInfo(paramUBO->getHandle(), 0, sizeof(ya::LitMaterial::ParamUBO));

    render
        ->getDescriptorHelper()
        ->updateDescriptorSets(
            {
                IDescriptorSetHelper::genBufferWrite(
                    ds,
                    0,
                    0,
                    EPipelineDescriptorType::UniformBuffer,
                    &bufferInfo,
                    1),
            },
            {});
}

void LitMaterialSystem::updateMaterialResourceDS(DescriptorSetHandle ds, LitMaterial *material)
{
    // auto render = getRender();


    // YA_CORE_ASSERT(ds.ptr != nullptr, "descriptor set is null: {}", _ctxEntityDebugStr);
    // auto &params = material->uMaterial;
    // // TODO: not texture and default texture?
    // // update param from texture
    // const TextureView *tv0 = material->getTextureView(UnlitMaterial::BaseColor0);
    // const TextureView *tv1 = material->getTextureView(UnlitMaterial::BaseColor1);

    // auto resourceUBO = _materialParamsUBOs[material->getIndex()].get();
    // resourceUBO->writeData(&params, sizeof(ya::UnlitMaterialUBO), 0);

    // DescriptorImageInfo imageInfo0(
    //     SamplerHandle(tv0->sampler->getHandle()),
    //     tv0->texture->getImageViewHandle(),
    //     EImageLayout::ShaderReadOnlyOptimal);
    // DescriptorImageInfo imageInfo1(
    //     SamplerHandle(tv1->sampler->getHandle()),
    //     tv1->texture->getImageViewHandle(),
    //     EImageLayout::ShaderReadOnlyOptimal);


    // render
    //     ->getDescriptorHelper()
    //     ->updateDescriptorSets(
    //         {
    //             IDescriptorSetHelper::genImageWrite(ds, 0, 0, EPipelineDescriptorType::CombinedImageSampler, &imageInfo0, 1),
    //             IDescriptorSetHelper::genImageWrite(ds, 1, 0, EPipelineDescriptorType::CombinedImageSampler, &imageInfo1, 1),
    //         }
    //         {});
}

void LitMaterialSystem::recreateMaterialDescPool(uint32_t _materialCount)
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
    // _materialResourceDSs.clear();

    // 3. recreate pool
    if (_materialDSP) {
        _materialDSP->resetPool();
    }
    DescriptorPoolCreateInfo poolCI{
        .maxSets   = newDescriptorSetCount, //* 2, // max(param , resource)
        .poolSizes = {
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::UniformBuffer,
                .descriptorCount = newDescriptorSetCount,
            },
            // DescriptorPoolSize{
            //     .type            = EPipelineDescriptorType::CombinedImageSampler,
            //     .descriptorCount = newDescriptorSetCount * 2, // tex0 + tex1 for each material param in one set
            // },
        },
    };
    _materialDSP = IDescriptorPool::create(render, poolCI);


    // 4. allocate new sets
    // 为每一个单独的材质分配描述符集
    _materialDSP->allocateDescriptorSets(_materialParamDSL, newDescriptorSetCount, _materialParamDSs);
    // _materialDSP->allocateDescriptorSets(_materialResourceDSL, newDescriptorSetCount, _materialResourceDSs);

    // 5. create UBOs
    uint32_t diffCount = newDescriptorSetCount - _materialParamsUBOs.size();
    for (uint32_t i = 0; i < diffCount; i++) {
        auto buffer = ya::IBuffer::create(
            render,
            ya::BufferCreateInfo{
                .label         = "LitMaterial_Param_UBO",
                .usage         = ya::EBufferUsage::UniformBuffer,
                .size          = sizeof(LitMaterial::ParamUBO),
                .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,
            });
        _materialParamsUBOs.push_back(buffer);
    }


    _lastMaterialDSCount = newDescriptorSetCount;
}

} // namespace ya
