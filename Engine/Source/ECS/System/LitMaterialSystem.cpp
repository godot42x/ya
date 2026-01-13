#include "LitMaterialSystem.h"
#include "Core/App/App.h"
#include "Core/Debug/Instrumentor.h"

#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"


#include "Render/Core/Buffer.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Swapchain.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Render.h"
#include "Render/TextureLibrary.h"


#include "Scene/Scene.h"
#include "glm/gtc/type_ptr.hpp"
#include "imgui.h"



namespace ya
{

void LitMaterialSystem::onInit(IRenderPass *renderPass)
{
    YA_PROFILE_FUNCTION();

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
                    // Debug UBO
                    DescriptorSetLayoutBinding{
                        .binding         = 2,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
            DescriptorSetLayout{
                .label    = "LitMaterial_Resource_DSL",
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
                },
            },
            DescriptorSetLayout{
                .label    = "LitMaterial_Param_DSL",
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
        },
    };

    auto DSLs            = IDescriptorSetLayout::create(render, pipelineLayout.descriptorSetLayouts);
    _materialFrameDSL    = DSLs[0];
    _materialResourceDSL = DSLs[1];
    _materialParamDSL    = DSLs[2];

    _pipelineLayout = IPipelineLayout::create(
        render,
        pipelineLayout.label,
        pipelineLayout.pushConstants,
        DSLs);


    // MARK: pipeline
    _pipelineDesc = GraphicsPipelineCreateInfo{
        .subPassRef = 0,
        .shaderDesc = ShaderDesc{
            .shaderName        = "Test/PhongLit.glsl",
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
            //
            .cullMode  = _cullMode,
            .frontFace = EFrontFaceType::CounterClockWise, // GL
            // .frontFace = EFrontFaceType::ClockWise, // VK: reverse viewport and front face to adapt vulkan
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
                    .descriptorCount = 3, // frame ubo + lighting ubo + debug ubo
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
    _debugUBO = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = "Lit_Debug_UBO",
            .usage         = ya::EBufferUsage::UniformBuffer,
            .size          = sizeof(LitMaterialSystem::DebugUBO),
            .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,
        });
}

void LitMaterialSystem::onDestroy()
{
}

void LitMaterialSystem::onUpdate(float deltaTime)
{
    YA_PROFILE_FUNCTION();

    auto scene = getActiveScene();
    YA_CORE_ASSERT(scene, "LitMaterialSystem::onUpdate - Scene is null");

    // Reset point light count
    uLight.numPointLights = 0;

    // Count entities with PointLightComponent
    auto   lightView   = scene->getRegistry().view<PointLightComponent, TransformComponent>();
    size_t entityCount = 0;
    for (auto _ : lightView) {
        entityCount++;
    }

    if (entityCount == 0) {
        // YA_CORE_WARN("LitMaterialSystem::onUpdate - No entities found with PointLightComponent + TransformComponent");
    }
    else {
        // YA_CORE_INFO("LitMaterialSystem::onUpdate - Found {} entities with PointLightComponent", entityCount);
    }

    // grab all point lights from scene (support up to MAX_POINT_LIGHTS)
    for (auto entity : scene->getRegistry().view<PointLightComponent, TransformComponent>()) {
        if (uLight.numPointLights >= MAX_POINT_LIGHTS) {
            YA_CORE_WARN("Exceeded maximum point lights ({}), ignoring additional lights", MAX_POINT_LIGHTS);
            break;
        }

        const auto &[plc, tc] = scene->getRegistry().get<PointLightComponent, TransformComponent>(entity);

        // Fill point light data
        auto &lightData     = uLight.pointLights[uLight.numPointLights];
        lightData.position  = tc.getPosition();
        lightData.intensity = plc._intensity;
        lightData.color     = plc._color;
        lightData.radius    = plc._range;

        // YA_CORE_INFO("  Light {}: pos=({:.2f},{:.2f},{:.2f}), intensity={:.2f}, radius={:.2f}",
        //              uLight.numPointLights,
        //              lightData.position.x, lightData.position.y, lightData.position.z,
        //              lightData.intensity, lightData.radius);

        uLight.numPointLights++;
    }

    // YA_CORE_INFO("LitMaterialSystem::onUpdate - Final numPointLights = {}", uLight.numPointLights);
}

void LitMaterialSystem::onRender(ICommandBuffer *cmdBuf, IRenderTarget *rt)
{
    YA_PROFILE_FUNCTION();

    Scene *scene = getActiveScene();
    if (!scene) {
        return;
    }
    auto view = scene->getRegistry().view<TagComponent, LitMaterialComponent, TransformComponent>();
    if (view.begin() == view.end()) {
        return;
    }

    // auto cmdBuffer = VulkanCommandBuffer::fromHandle(cmdBuf);
    {
        YA_PROFILE_SCOPE("LitMaterial::BindPipeline");
        cmdBuf->bindPipeline(_pipeline.get());
    }

    uint32_t width  = rt->getFrameBuffer()->getWidth();
    uint32_t height = rt->getFrameBuffer()->getHeight();

    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(height);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(height);
        viewportHeight = -static_cast<float>(height);
    }

    {
        YA_PROFILE_SCOPE("LitMaterial::SetViewportScissorCull");
        cmdBuf->setViewport(0.0f, viewportY, (float)width, viewportHeight, 0.0f, 1.0f);
        cmdBuf->setScissor(0, 0, width, height);
        cmdBuf->setCullMode(_cullMode);
    }

    {
        YA_PROFILE_SCOPE("LitMaterial::UpdateFrameDS");
        updateFrameDS(rt);
    }

    bool     bShouldForceUpdateMaterial = false;
    uint32_t materialCount              = MaterialFactory::get()->getMaterialSize<LitMaterial>();
    if (materialCount > _lastMaterialDSCount) {
        YA_PROFILE_SCOPE("LitMaterial::RecreateMaterialDescPool");
        recreateMaterialDescPool(materialCount);
        bShouldForceUpdateMaterial = true;
    }

    std::vector<int> updatedMaterial(materialCount, 0);

    YA_PROFILE_SCOPE("LitMaterial::EntityLoop");
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
            DescriptorSetHandle resourceDS            = _materialResourceDSs[materialInstanceIndex];
            DescriptorSetHandle paramDS               = _materialParamDSs[materialInstanceIndex];

            // TODO: 拆分更新 descriptor set 和 draw call 为两个循环？ 能否优化效率?
            if (!updatedMaterial[materialInstanceIndex]) {
                if (bShouldForceUpdateMaterial || material->isResourceDirty())
                {
                    YA_PROFILE_SCOPE("LitMaterial::UpdateResourceDS");
                    updateMaterialResourceDS(resourceDS, material);
                    material->setResourceDirty(false);
                }
                if (bShouldForceUpdateMaterial || material->isParamDirty())
                {
                    YA_PROFILE_SCOPE("LitMaterial::UpdateParamDS");
                    updateMaterialParamDS(paramDS, material);
                    material->setParamDirty(false);
                }

                updatedMaterial[materialInstanceIndex] = true;
            }

            // bind descriptor set
            {
                YA_PROFILE_SCOPE("LitMaterial::BindDescriptorSets");
                std::vector<DescriptorSetHandle> descSets = {
                    _frameDS,
                    resourceDS,
                    paramDS,
                };
                cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descSets);
            }

            // update push constant
            {
                YA_PROFILE_SCOPE("LitMaterial::PushConstants");
                LitMaterialSystem::ModelPushConstant pushConst{
                    .modelMat = tc.getTransform(),
                };
                cmdBuf->pushConstants(_pipelineLayout.get(),
                                      EShaderStage::Vertex,
                                      0,
                                      sizeof(LitMaterialSystem::ModelPushConstant),
                                      &pushConst);
            }

            // draw each mesh
            {
                YA_PROFILE_SCOPE("LitMaterial::DrawMeshes");
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
}

void LitMaterialSystem::onRenderGUI()
{
    IMaterialSystem::onRenderGUI();
    ImGui::Text("Directional Light");
    ImGui::Indent();
    {
        ImGui::PushID("1");
        ImGui::DragFloat3("Direction", glm::value_ptr(uLight.dirLight.direction), 0.1f);
        ImGui::ColorEdit3("Color", glm::value_ptr(uLight.dirLight.color));
        ImGui::SliderFloat("Intensity", &uLight.dirLight.intensity, 0.0f, 10.0f);
        ImGui::ColorEdit3("Ambient", glm::value_ptr(uLight.dirLight.ambient));
        ImGui::DragFloat("Constant", &uLight.dirLight.constant, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Linear", &uLight.dirLight.linear, 0.001f, 0.0f, 1.0f);
        ImGui::DragFloat("Quadratic", &uLight.dirLight.quadratic, 0.001f, 0.0f, 1.0f);
        ImGui::PopID();
    }
    ImGui::Unindent();
    ImGui::Separator();


    if (ImGui::CollapsingHeader("Debug Options", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::Checkbox("Debug Normal", &uDebug.bDebugNormal);
        ImGui::DragFloat4("Float Param", glm::value_ptr(uDebug.floatParam), 0.1f);
        ImGui::Unindent();
    }
};


// TODO: descriptor set can be shared if they use same layout and data
void LitMaterialSystem::updateFrameDS(IRenderTarget *rt)
{
    YA_PROFILE_FUNCTION();

    auto app    = getApp();
    auto render = getRender();

    glm::mat4 proj;
    glm::mat4 view;
    rt->getViewAndProjMatrix(view, proj);

    // 从 view matrix 提取相机位置
    // glm::mat4 invView   = glm::inverse(view);
    glm::mat4 invView   = view;
    glm::vec3 cameraPos = glm::vec3(invView[3]);

    FrameUBO uFrame{
        .projection = proj,
        .view       = view,
        .resolution = {
            rt->getFrameBuffer()->getWidth(),
            rt->getFrameBuffer()->getHeight(),
        },
        .frameIndex = app->getFrameIndex(),
        .time       = (float)app->getElapsedTimeMS() / 1000.0f,
        .cameraPos  = cameraPos,
    };

    _frameUBO->writeData(&uFrame, sizeof(FrameUBO), 0);
    _lightUBO->writeData(&uLight, sizeof(LightUBO), 0);
    _debugUBO->writeData(&uDebug, sizeof(DebugUBO), 0);


    render
        ->getDescriptorHelper()
        ->updateDescriptorSets(
            {
                IDescriptorSetHelper::genSingleBufferWrite(_frameDS, 0, EPipelineDescriptorType::UniformBuffer, _frameUBO.get()),
                IDescriptorSetHelper::genSingleBufferWrite(_frameDS, 1, EPipelineDescriptorType::UniformBuffer, _lightUBO.get()),
                IDescriptorSetHelper::genSingleBufferWrite(_frameDS, 2, EPipelineDescriptorType::UniformBuffer, _debugUBO.get()),
            },
            {});
}

void LitMaterialSystem::updateMaterialParamDS(DescriptorSetHandle ds, LitMaterial *material)
{
    YA_PROFILE_FUNCTION();

    auto render = getRender();
    YA_CORE_ASSERT(ds.ptr != nullptr, "descriptor set is null: {}", _ctxEntityDebugStr);


    auto &params = material->getParamsMut();
    // update param from texture
    // const TextureView *tv0 = material->getTextureView(UnlitMaterial::BaseColor0);
    // if (tv0) {
    //     Material::updateTextureParamsByTextureView(tv0, params.textureParam0);
    // }
    // const TextureView *tv1 = material->getTextureView(LitMaterial::BaseColor1);
    // if (tv1) {
    //     Material::updateTextureParamsByTextureView(tv1, params.textureParam1);
    // }

    auto paramUBO = _materialParamsUBOs[material->getIndex()];
    paramUBO->writeData(&params, sizeof(material_param_t), 0);

    DescriptorBufferInfo bufferInfo(paramUBO->getHandle(), 0, sizeof(material_param_t));

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

void LitMaterialSystem::updateMaterialResourceDS(DescriptorSetHandle ds, LitMaterial *material)
{
    YA_PROFILE_FUNCTION();

    auto render = getRender();

    YA_CORE_ASSERT(ds.ptr != nullptr, "descriptor set is null: {}", _ctxEntityDebugStr);

    DescriptorImageInfo diffuseTexture  = getDescriptorImageInfo(material->getTextureView(LitMaterial::EResource::DiffuseTexture));
    DescriptorImageInfo specularTexture = getDescriptorImageInfo(material->getTextureView(LitMaterial::EResource::SpecularTexture));

    render
        ->getDescriptorHelper()
        ->updateDescriptorSets(
            {
                IDescriptorSetHelper::genImageWrite(ds, 0, 0, EPipelineDescriptorType::CombinedImageSampler, {diffuseTexture}),
                IDescriptorSetHelper::genImageWrite(ds, 1, 0, EPipelineDescriptorType::CombinedImageSampler, {specularTexture}),
            },
            {});
}

void LitMaterialSystem::recreateMaterialDescPool(uint32_t _materialCount)
{
    YA_PROFILE_FUNCTION();

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

    for (auto &ds : _materialParamDSs) {
        YA_CORE_ASSERT(ds.ptr != nullptr, "Failed to allocate material param descriptor set");
    }

    // 5. create UBOs
    uint32_t diffCount = newDescriptorSetCount - _materialParamsUBOs.size();
    for (uint32_t i = 0; i < diffCount; i++) {
        auto buffer = ya::IBuffer::create(
            render,
            ya::BufferCreateInfo{
                .label         = "LitMaterial_Param_UBO",
                .usage         = ya::EBufferUsage::UniformBuffer,
                .size          = sizeof(material_param_t),
                .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,
            });
        _materialParamsUBOs.push_back(buffer);
    }


    _lastMaterialDSCount = newDescriptorSetCount;
}

DescriptorImageInfo LitMaterialSystem::getDescriptorImageInfo(TextureView const *tv)
{
    SamplerHandle   samplerHandle;
    ImageViewHandle imageViewHandle;
    if (!tv) {
        samplerHandle   = TextureLibrary::getDefaultSampler()->getHandle();
        imageViewHandle = TextureLibrary::getWhiteTexture()->getImageViewHandle();
    }
    else {
        samplerHandle   = SamplerHandle(tv->sampler->getHandle());
        imageViewHandle = tv->texture->getImageViewHandle();
    }

    DescriptorImageInfo imageInfo0(samplerHandle, imageViewHandle, EImageLayout::ShaderReadOnlyOptimal);
    return imageInfo0;
}

} // namespace ya
