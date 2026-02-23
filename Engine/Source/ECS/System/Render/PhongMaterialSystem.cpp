#include "PhongMaterialSystem.h"

#include "Core/App/App.h"
#include "Core/Debug/Instrumentor.h"
#include <algorithm>


#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/MirrorComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"



#include "Editor/TypeRenderer.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Swapchain.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Render.h"
#include "Resource/TextureLibrary.h"


#include "Scene/Scene.h"
#include "imgui.h"

#include "Core/Math/Math.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/mat3x3.hpp"



namespace ya
{

void PhongMaterialSystem::onInitImpl(const InitParams& initParams)
{
    YA_PROFILE_FUNCTION();

    IRender* render = getRender();

    // MARK: layout

    auto DSLs            = IDescriptorSetLayout::create(render, _pipelineLayoutDesc.descriptorSetLayouts);
    _materialFrameDSL    = DSLs[0];
    _materialResourceDSL = DSLs[1];
    _materialParamDSL    = DSLs[2];
    // _skyBoxCbeMapDSL     = DSLs[3];

    _pipelineLayout = IPipelineLayout::create(
        render,
        _pipelineLayoutDesc.label,
        _pipelineLayoutDesc.pushConstants,
        DSLs);


    // MARK: pipeline
    _pipelineDesc = GraphicsPipelineCreateInfo{
        .renderPass            = initParams.renderPass,
        .pipelineRenderingInfo = initParams.pipelineRenderingInfo,
        .pipelineLayout        = _pipelineLayout.get(),

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
            .defines = {
                "MAX_POINT_LIGHTS=" + std::to_string(PhongMaterialSystem::MAX_POINT_LIGHTS),
            }},
        // define what state need to dynamically modified in render pass execution
        .dynamicFeatures = {
            EPipelineDynamicFeature::Scissor, // the imgui required this feature as I did not set the dynamical render feature
            EPipelineDynamicFeature::Viewport,
        },
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            //
            .cullMode  = ECullMode::Back,
            .frontFace = EFrontFaceType::CounterClockWise, // GL
                                                           // .frontFace = EFrontFaceType::ClockWise, // VK: reverse viewport and front face to adapt vulkan
        },
        .multisampleState = MultisampleState{
            .sampleCount = ESampleCount::Sample_1,
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
                    // index of the attachments in the render pass and the renderpass begin info
                    .index               = 0,
                    .bBlendEnable        = true,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,         // srcColor = srcColor * srcAlpha
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha, // dstColor = dstColor * (1 - srcAlpha)
                    .colorBlendOp        = EBlendOp::Add,                  // finalColor = srcColor + dstColor
                    .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,         // use src alpha for alpha blending
                    .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha, // use dst alpha for alpha blending
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
    _pipeline = IGraphicsPipeline::create(render);
    _pipeline->recreate(_pipelineDesc);


    _frameDSP = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = MAX_PASS_SLOTS,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 3 * MAX_PASS_SLOTS, // (frame + lighting + debug) * slots
                },
            },
        });
    std::vector<ya::DescriptorSetHandle> sets;
    _frameDSP->allocateDescriptorSets(_materialFrameDSL, MAX_PASS_SLOTS, sets);
    for (uint32_t i = 0; i < MAX_PASS_SLOTS; ++i) {
        _frameDSs[i] = sets[i];
    }

    // TODO: create a auto extend descriptor pool class to support recreate
    recreateMaterialDescPool(NUM_MATERIAL_BATCH);

    std::vector<WriteDescriptorSet> writes;
    for (uint32_t i = 0; i < MAX_PASS_SLOTS; ++i) {
        _frameUBOs[i] = IBuffer::create(
            render,
            ya::BufferCreateInfo{
                .label         = std::format("Lit_Frame_UBO_{}", i),
                .usage         = ya::EBufferUsage::UniformBuffer,
                .size          = sizeof(PhongMaterialSystem::FrameUBO),
                .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,
            });
        _lightUBOs[i] = IBuffer::create(
            render,
            ya::BufferCreateInfo{
                .label         = std::format("Lit_Light_UBO_{}", i),
                .usage         = ya::EBufferUsage::UniformBuffer,
                .size          = sizeof(PhongMaterialSystem::LightUBO),
                .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,
            });
        _debugUBOs[i] = IBuffer::create(
            render,
            ya::BufferCreateInfo{
                .label         = std::format("Lit_Debug_UBO_{}", i),
                .usage         = ya::EBufferUsage::UniformBuffer,
                .size          = sizeof(PhongMaterialSystem::DebugUBO),
                .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,
            });

        writes.push_back(IDescriptorSetHelper::genSingleBufferWrite(_frameDSs[i], 0, EPipelineDescriptorType::UniformBuffer, _frameUBOs[i].get()));
        writes.push_back(IDescriptorSetHelper::genSingleBufferWrite(_frameDSs[i], 1, EPipelineDescriptorType::UniformBuffer, _lightUBOs[i].get()));
        writes.push_back(IDescriptorSetHelper::genSingleBufferWrite(_frameDSs[i], 2, EPipelineDescriptorType::UniformBuffer, _debugUBOs[i].get()));
    }

    render->getDescriptorHelper()->updateDescriptorSets(writes, {});
    render->waitIdle();
    // where to create pipeline? -> on frame begin -> bDirty
}

void PhongMaterialSystem::onDestroy()
{
}

// MARK: grab resources
void PhongMaterialSystem::preTick(float deltaTime, const FrameContext* ctx)
{
    YA_PROFILE_FUNCTION();

    auto scene = getActiveScene();
    YA_CORE_ASSERT(scene, "PhongMaterialSystem::onUpdate - Scene is null");

    {
        bool bFoundDirectionalLight = false;
        for (const auto& [entity, dlc, tc] : scene->getRegistry().view<DirectionalLightComponent, TransformComponent>().each()) {
            uLight.dirLight.direction = glm::normalize(tc.getForward());
            uLight.dirLight.ambient   = dlc._ambient;
            uLight.dirLight.diffuse   = dlc._diffuse;
            uLight.dirLight.specular  = dlc._specular;
            bFoundDirectionalLight    = true;
            break;
        }

        if (!bFoundDirectionalLight) {
            for (const auto& [entity, dlc] : scene->getRegistry().view<DirectionalLightComponent>().each()) {
                uLight.dirLight.direction = glm::normalize(dlc._direction);
                uLight.dirLight.ambient   = dlc._ambient;
                uLight.dirLight.diffuse   = dlc._diffuse;
                uLight.dirLight.specular  = dlc._specular;
                break;
            }
        }
    }

    // grab all point lights from scene (support up to MAX_POINT_LIGHTS)
    // Reset point light count
    uLight.numPointLights = 0;
    for (const auto& [entity, plc, tc] : scene->getRegistry().view<PointLightComponent, TransformComponent>().each()) {
        if (uLight.numPointLights >= MAX_POINT_LIGHTS) {
            YA_CORE_WARN("Exceeded maximum point lights ({}), ignoring additional lights", MAX_POINT_LIGHTS);
            break;
        }

        // Fill point light data
        uLight.pointLights[uLight.numPointLights] = PointLightData{
            .type      = (float)plc._type,
            .constant  = plc._constant,
            .linear    = plc._linear,
            .quadratic = plc._quadratic,
            .position  = tc._position,

            .ambient  = plc._ambient,
            .diffuse  = plc._diffuse,
            .specular = plc._specular,

            .spotDir     = tc.getForward(),
            .innerCutOff = glm::cos(glm::radians(plc._innerConeAngle)),
            .outerCutOff = glm::cos(glm::radians(plc._outerConeAngle)),
        };
        // YA_CORE_INFO("  Light {}: pos=({:.2f},{:.2f},{:.2f}), intensity={:.2f}, radius={:.2f}",
        //              uLight.numPointLights,
        //              lightData.position.x, lightData.position.y, lightData.position.z,
        //              lightData.intensity, lightData.radius);

        uLight.numPointLights++;
    }
    // This prevents descriptor set invalidation during the render loop
    {
        uint32_t materialCount = MaterialFactory::get()->getMaterialSize<PhongMaterial>();
        if (materialCount > _lastMaterialDSCount) {
            YA_PROFILE_SCOPE("PhongMaterial::RecreateMaterialDescPool");
            recreateMaterialDescPool(materialCount);
            _bDescriptorPoolRecreated = true;
        }
    }
}

// MARK: render
void PhongMaterialSystem::onRender(ICommandBuffer* cmdBuf, const FrameContext* ctx)
{
    YA_PROFILE_FUNCTION();

    Scene* scene = getActiveScene();
    if (!scene) {
        return;
    }

    preTick(0.0f, ctx);


    // Query entities with both PhongMaterialComponent and MeshComponent
    auto view = scene->getRegistry().view<PhongMaterialComponent, MeshComponent, TransformComponent>();
    if (view.begin() == view.end()) {
        return;
    }
    // auto view2 = scene->getRegistry().view<UIComponent,TransformComponent>();

    {
        YA_PROFILE_SCOPE("PhongMaterial::BindPipeline");
        cmdBuf->bindPipeline(_pipeline.get());
    }

    // Get viewport extent from App (since we no longer have direct RT access)
    uint32_t width  = ctx->extent.width;
    uint32_t height = ctx->extent.height;
    if (width == 0 || height == 0) {
        return;
    }

    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(height);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(height);
        viewportHeight = -static_cast<float>(height);
    }

    {
        YA_PROFILE_SCOPE("PhongMaterial::SetViewportScissorCull");
        cmdBuf->setViewport(0.0f, viewportY, (float)width, viewportHeight, 0.0f, 1.0f);
        cmdBuf->setScissor(0, 0, width, height);
    }

    updateFrameDS(ctx);


    // Phase 3: Render loop
    YA_PROFILE_SCOPE("PhongMaterial::EntityLoop");

    // sort by z to render farthest objects first
    std::vector<std::pair<entt::entity, const TransformComponent*>> entries;
    {
        YA_PROFILE_SCOPE("PhongMaterial::SortByZ");
        if (ctx->viewOwner != entt::null) {
            for (const auto& [entity, lmc, mc, tc] : view.each()) {
                if (entity == ctx->viewOwner) {
                    continue;
                }
                entries.emplace_back(entity, &tc);
            }
        }
        else {
            for (const auto& [entity, lmc, mc, tc] : view.each()) {
                entries.emplace_back(entity, &tc);
            }
        }
        std::ranges::sort(entries, [](const auto& a, const auto& b) {
            // return a.second->getWorldPosition().z > b.second->getWorldPosition().z;
            // world forward is -Z in right-hand system
            return a.second->getWorldPosition().z < b.second->getWorldPosition().z;
        });
    }

    entt::entity mirrorID = entt::null;
    for (const auto& [id, mc, pmc] : scene->getRegistry().view<MirrorComponent, PhongMaterialComponent>().each()) {
        mirrorID = id;
        break;
    }


    // Material tracking for this frame
    uint32_t         materialCount = MaterialFactory::get()->getMaterialSize<PhongMaterial>();
    std::vector<int> updatedMaterial(materialCount, 0);

    for (auto& [entity, tc] : entries)
    {
        const auto& [lmc, meshComp, tc2] = view.get(entity);

        Entity* entityPtr = scene->getEntityByEnttID(entity);

        // Get runtime material from component
        PhongMaterial* material = lmc.getMaterial();
        if (!material || material->getIndex() < 0)
        {
            YA_CORE_WARN("PhongMaterialSystem: Entity '{}' has no valid material",
                         entityPtr ? entityPtr->getName() : "Unknown");
            continue;
        }
        _ctxEntityDebugStr = std::format("{} (Mat: {})", entityPtr ? entityPtr->getName() : "Unknown", material->getLabel());


        // update each material instance's descriptor set if dirty
        uint32_t            materialInstanceIndex = material->getIndex();
        DescriptorSetHandle resourceDS            = _materialResourceDSs[materialInstanceIndex];
        DescriptorSetHandle paramDS               = _materialParamDSs[materialInstanceIndex];

        // TODO: 拆分更新 descriptor set 和 draw call 为两个循环？ 能否优化效率?
        if (App::get()->bRenderMirror) {
            if (!updatedMaterial[materialInstanceIndex]) {
                // FIXME: hack for now update the mirror material every time
                bool bOverrideMirrorMaterial = (entity == mirrorID);
                if (_bDescriptorPoolRecreated || material->isResourceDirty() || bOverrideMirrorMaterial)
                {
                    YA_PROFILE_SCOPE("PhongMaterial::UpdateResourceDS");
                    updateMaterialResourceDS(resourceDS, material, bOverrideMirrorMaterial);
                    material->setResourceDirty(false);
                }
                if (_bDescriptorPoolRecreated || material->isParamDirty() || bOverrideMirrorMaterial)
                {
                    YA_PROFILE_SCOPE("PhongMaterial::UpdateParamDS");
                    updateMaterialParamDS(paramDS, lmc, bOverrideMirrorMaterial, _bDescriptorPoolRecreated);
                    material->setParamDirty(false);
                }

                updatedMaterial[materialInstanceIndex] = true;
            }
        }
        else {
            if (!updatedMaterial[materialInstanceIndex]) {
                if (_bDescriptorPoolRecreated || material->isResourceDirty())
                {
                    YA_PROFILE_SCOPE("PhongMaterial::UpdateResourceDS");
                    updateMaterialResourceDS(resourceDS, material, false);
                    material->setResourceDirty(false);
                }
                if (_bDescriptorPoolRecreated || material->isParamDirty())
                {
                    YA_PROFILE_SCOPE("PhongMaterial::UpdateParamDS");
                    updateMaterialParamDS(paramDS, lmc, false, _bDescriptorPoolRecreated);
                    material->setParamDirty(false);
                }

                updatedMaterial[materialInstanceIndex] = true;
            }
        }

        // bind descriptor set
        {
            YA_PROFILE_SCOPE("PhongMaterial::BindDescriptorSets");
            cmdBuf->bindDescriptorSets(_pipelineLayout.get(),
                                       0,
                                       {
                                           _frameDSs[getPassSlot()],
                                           resourceDS,
                                           paramDS,
                                           skyBoxCubeMapDS,
                                           depthBufferDS,
                                       });
        }

        // update push constant
        {
            YA_PROFILE_SCOPE("PhongMaterial::PushConstants");
            PhongMaterialSystem::ModelPushConstant pushConst{
                .modelMat = tc->getTransform(),
            };
            cmdBuf->pushConstants(_pipelineLayout.get(),
                                  _pipelineLayoutDesc.pushConstants[0].stageFlags,
                                  0,
                                  sizeof(PhongMaterialSystem::ModelPushConstant),
                                  &pushConst);
        }

        // draw mesh from MeshComponent (single mesh per component)
        {
            YA_PROFILE_SCOPE("PhongMaterial::DrawMesh");
            Mesh* mesh = meshComp.getMesh();
            if (mesh) {
                mesh->draw(cmdBuf);
            }
        }
    }

    // Reset force update flag after rendering
    _bDescriptorPoolRecreated = false;
    advanceSlot(); // Advance pass slot for next frame (ring buffer)
}

void PhongMaterialSystem::onRenderGUI()
{
    using namespace ImGui;
    IMaterialSystem::onRenderGUI();

    TextColored(ImColor(1.0f, 1.0f, 0.0f, 1.0f), "pass slot: %d", getPassSlot());

    // if (TreeNode("Directional Light")) {
    //     ya::RenderContext ctx;
    //     // ya::renderReflectedType("DirectionalLight", ya::type_index_v<PhongMaterialSystem::DirectionalLightData>, &uLight.dirLight, ctx);
    //     TreePop();
    // }


    if (ImGui::TreeNode("Debug Options")) {
        ImGui::Checkbox("Debug Normal", &uDebug.bDebugNormal);
        ImGui::Checkbox("Debug Depth", &uDebug.bDebugDepth);
        ImGui::Checkbox("Debug UV", &uDebug.bDebugUV);
        ImGui::DragFloat4("Float Param", glm::value_ptr(uDebug.floatParam), 0.1f);
        TreePop();
    }
};


// TODO: descriptor set can be shared if they use same layout and data
void PhongMaterialSystem::updateFrameDS(const FrameContext* ctx)
{
    YA_PROFILE_FUNCTION();

    auto app    = getApp();
    auto render = getRender();

    // Use passed camera context
    FrameUBO uFrame{
        .projection = ctx->projection,
        .view       = ctx->view,
        .resolution = {
            ctx->extent.width,
            ctx->extent.height,
        },
        .frameIndex = app->getFrameIndex(),
        .time       = (float)app->getElapsedTimeMS() / 1000.0f,
        .cameraPos  = ctx->cameraPos,
    };

    // TODO: handle the rotation  of radians in shader
    // auto light               = uLight;
    // light.dirLight.direction = glm::radians(light.dirLight.direction);

    uint32_t slot = getPassSlot();

    _frameUBOs[slot]->writeData(&uFrame, sizeof(FrameUBO), 0);
    _lightUBOs[slot]->writeData(&uLight, sizeof(LightUBO), 0);
    _debugUBOs[slot]->writeData(&uDebug, sizeof(DebugUBO), 0);


    if (_bDescriptorPoolRecreated) {
        for (uint32_t i = 0; i < MAX_PASS_SLOTS; ++i) {
            render
                ->getDescriptorHelper()
                ->updateDescriptorSets(
                    {
                        IDescriptorSetHelper::writeOneUniformBuffer(_frameDSs[i], 0, _frameUBOs[i].get()),
                        IDescriptorSetHelper::writeOneUniformBuffer(_frameDSs[i], 1, _lightUBOs[i].get()),
                        IDescriptorSetHelper::writeOneUniformBuffer(_frameDSs[i], 2, _debugUBOs[i].get()),
                    },
                    {});
        }
    }
}

void PhongMaterialSystem::updateMaterialParamDS(DescriptorSetHandle ds, PhongMaterialComponent& component, bool bOverrideDiffuse, bool bRecreated)
{
    YA_PROFILE_FUNCTION();

    auto render = getRender();
    YA_CORE_ASSERT(ds.ptr != nullptr, "descriptor set is null: {}", _ctxEntityDebugStr);

    PhongMaterial* material = component.getMaterial();
    auto&          params   = material->getParamsMut();

    // Read UV params directly from TextureSlot (single source of truth)
    auto& diffuseTexParam = params.textureParams[PhongMaterial::EResource::DiffuseTexture];
    if (auto* slot = component.getTextureSlot(PhongMaterial::EResource::DiffuseTexture); slot && slot->isValid()) {

        diffuseTexParam.uvTransform = FMath::build_transform_mat3(
            slot->uvOffset,
            slot->uvRotation,
            slot->uvScale);
        diffuseTexParam.bEnable = slot->bEnable;
    }
    else {
        diffuseTexParam.bEnable = false;
    }

    auto& specularTexParam = params.textureParams[PhongMaterial::EResource::SpecularTexture];
    if (auto* slot = component.getTextureSlot(PhongMaterial::EResource::SpecularTexture); slot && slot->isValid()) {
        specularTexParam.uvTransform = FMath::build_transform_mat3(
            slot->uvOffset,
            slot->uvRotation,
            slot->uvScale);
        specularTexParam.bEnable = slot->bEnable;
    }
    else {
        specularTexParam.bEnable = false;
    }

    auto& reflectionTexParam = params.textureParams[PhongMaterial::EResource::ReflectionTexture];
    if (auto* slot = component.getTextureSlot(PhongMaterial::EResource::ReflectionTexture); slot && slot->isValid()) {
        reflectionTexParam.uvTransform = FMath::build_transform_mat3(
            slot->uvOffset,
            slot->uvRotation,
            slot->uvScale);
        reflectionTexParam.bEnable = slot->bEnable;
    }
    else {
        reflectionTexParam.bEnable = false;
    }

    auto paramUBO = _materialParamsUBOs[material->getIndex()];
    paramUBO->writeData(&params, sizeof(material_param_t), 0);


    // ubo already bound to the ds, no need to update, except for recreation
    // if (bRecreated || bOverrideDiffuse) { // 2026/2/21 @godot42: ignore performance now...
    // FIXME: why must bOverrideDiffuse be true then the mirror texture render success?
    render
        ->getDescriptorHelper()
        ->updateDescriptorSets(
            {
                IDescriptorSetHelper::genSingleBufferWrite(ds, 0, EPipelineDescriptorType::UniformBuffer, paramUBO.get()),
            },
            {});
    // }
}

void PhongMaterialSystem::updateMaterialResourceDS(DescriptorSetHandle ds, PhongMaterial* material, bool bOverrideDiffuse)
{
    YA_PROFILE_FUNCTION();

    auto render = getRender();

    YA_CORE_ASSERT(ds.ptr != nullptr, "descriptor set is null: {}", _ctxEntityDebugStr);

    auto diffuseTV    = material->getTextureView(PhongMaterial::EResource::DiffuseTexture);
    auto specularTV   = material->getTextureView(PhongMaterial::EResource::SpecularTexture);
    auto reflectionTV = material->getTextureView(PhongMaterial::EResource::ReflectionTexture);

    DescriptorImageInfo diffuseTexture    = getDescriptorImageInfo(diffuseTV);
    DescriptorImageInfo specularTexture   = getDescriptorImageInfo(specularTV);
    DescriptorImageInfo reflectionTexture = getDescriptorImageInfo(reflectionTV);
    //  mirror or other rt?
    if (bOverrideDiffuse && App::get()->bHasMirror) {
        auto mirrorTexture = App::get()->_mirrorRT->getCurFrameBuffer()->getColorTexture(0);
        diffuseTexture     = getDescriptorImageInfo(mirrorTexture->getImageView(), nullptr);
    }

    render
        ->getDescriptorHelper()
        ->updateDescriptorSets(
            {
                IDescriptorSetHelper::genImageWrite(ds, 0, 0, EPipelineDescriptorType::CombinedImageSampler, {diffuseTexture}),
                IDescriptorSetHelper::genImageWrite(ds, 1, 0, EPipelineDescriptorType::CombinedImageSampler, {specularTexture}),
                IDescriptorSetHelper::genImageWrite(ds, 2, 0, EPipelineDescriptorType::CombinedImageSampler, {reflectionTexture}),
            },
            {});
}

void PhongMaterialSystem::recreateMaterialDescPool(uint32_t _materialCount)
{
    YA_PROFILE_FUNCTION();

    auto* render = getRender();
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
        .maxSets   = newDescriptorSetCount * (1 + 3),
        .poolSizes = {
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::UniformBuffer,
                .descriptorCount = newDescriptorSetCount,
            },
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::CombinedImageSampler,
                .descriptorCount = newDescriptorSetCount * 3, // tex0 + tex1  + tex2 for each material param in one set
            },
        },
    };
    _materialDSP = IDescriptorPool::create(render, poolCI);


    // 4. allocate new sets
    // 为每一个单独的材质分配描述符集
    _materialDSP->allocateDescriptorSets(_materialParamDSL, newDescriptorSetCount, _materialParamDSs);
    _materialDSP->allocateDescriptorSets(_materialResourceDSL, newDescriptorSetCount, _materialResourceDSs);

    for (auto& ds : _materialParamDSs) {
        YA_CORE_ASSERT(ds.ptr != nullptr, "Failed to allocate material param descriptor set");
    }

    // 5. create UBOs
    uint32_t                        diffCount = newDescriptorSetCount - _materialParamsUBOs.size();
    std::vector<WriteDescriptorSet> writes;
    for (uint32_t i = 0; i < diffCount; i++) {
        auto buffer = ya::IBuffer::create(
            render,
            ya::BufferCreateInfo{
                .label         = "PhongMaterial_Param_UBO",
                .usage         = ya::EBufferUsage::UniformBuffer,
                .size          = sizeof(material_param_t),
                .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,
            });
        _materialParamsUBOs.push_back(buffer);
        writes.push_back(IDescriptorSetHelper::genSingleBufferWrite(_materialParamDSs[i], 0, EPipelineDescriptorType::UniformBuffer, buffer.get()));
    }
    render->getDescriptorHelper()->updateDescriptorSets(writes, {});


    _lastMaterialDSCount = newDescriptorSetCount;
}

} // namespace ya
