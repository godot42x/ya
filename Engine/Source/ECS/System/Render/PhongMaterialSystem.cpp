#include "PhongMaterialSystem.h"

#include "Runtime/App/App.h"
#include "Core/Debug/Instrumentor.h"
#include <algorithm>


#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/MirrorComponent.h"
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

static std::vector<std::string> buildPhongShaderDefines(bool bEnableDirectionalShadow)
{
    std::vector<std::string> defines;
    if (bEnableDirectionalShadow) {
        defines.push_back("ENABLE_DIRECTIONAL_SHADOW 1");
    }
    return defines;
}

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
            // .shaderName        = "PhongLit.slang",
            .shaderName        = "PhongLit/PhongLit.glsl",
            .bDeriveFromShader = false,
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(ya::Vertex),
                },
            },
            .vertexAttributes = _pipelineLayoutDesc.vertexAttributes,
            .defines          = buildPhongShaderDefines(_bDirectionalShadowMapping),
        },
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

    // TODO: create a auto extend descriptor pool class to support recreate
    recreateMaterialDescPool(NUM_MATERIAL_BATCH);
    // where to create pipeline? -> on frame begin -> bDirty
    uDebug.bDebugDepth  = 0;
    uDebug.bDebugNormal = 0;
    uDebug.bDebugUV     = 0;
}

void PhongMaterialSystem::onDestroy()
{
    _currentScenePassResources.reset();
}

stdptr<PhongScenePassResources> PhongMaterialSystem::createScenePassResources(const std::string& label, const stdptr<IBuffer>& sharedLightUBO)
{
    auto render      = getRender();
    auto resources   = makeShared<PhongScenePassResources>();
    resources->label = label;

    resources->frameDSP = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = 1,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 3,
                },
            },
        });

    std::vector<DescriptorSetHandle> sets;
    resources->frameDSP->allocateDescriptorSets(_materialFrameDSL, 1, sets);
    YA_CORE_ASSERT(!sets.empty(), "Failed to allocate Phong scene pass descriptor set: {}", label);
    resources->frameDS = sets[0];

    resources->frameUBO = IBuffer::create(
        render,
        BufferCreateInfo{
            .label         = std::format("{}_Frame_UBO", label),
            .usage         = EBufferUsage::UniformBuffer,
            .size          = sizeof(FrameUBO),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
        });
    resources->lightUBO = sharedLightUBO;
    if (!resources->lightUBO) {
        resources->lightUBO = IBuffer::create(
            render,
            BufferCreateInfo{
                .label         = std::format("{}_Light_UBO", label),
                .usage         = EBufferUsage::UniformBuffer,
                .size          = sizeof(LightUBO),
                .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
            });
    }
    resources->debugUBO = IBuffer::create(
        render,
        BufferCreateInfo{
            .label         = std::format("{}_Debug_UBO", label),
            .usage         = EBufferUsage::UniformBuffer,
            .size          = sizeof(DebugUBO),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
        });

    render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genSingleBufferWrite(resources->frameDS, 0, EPipelineDescriptorType::UniformBuffer, resources->frameUBO.get()),
            IDescriptorSetHelper::genSingleBufferWrite(resources->frameDS, 1, EPipelineDescriptorType::UniformBuffer, resources->lightUBO.get()),
            IDescriptorSetHelper::genSingleBufferWrite(resources->frameDS, 2, EPipelineDescriptorType::UniformBuffer, resources->debugUBO.get()),
        },
        {});

    return resources;
}

void PhongMaterialSystem::setScenePassResources(const stdptr<PhongScenePassResources>& resources)
{
    _currentScenePassResources = resources;
}

void PhongMaterialSystem::uploadSharedLightUBO(const FrameContext* ctx, IBuffer* sharedLightUBO)
{
    YA_CORE_ASSERT(ctx, "FrameContext is null when uploading shared light UBO");
    YA_CORE_ASSERT(sharedLightUBO, "Shared light UBO is null");

    fillLightUBOFromFrameContext(ctx);
    sharedLightUBO->writeData(&uLight, sizeof(LightUBO), 0);
}

void PhongMaterialSystem::fillLightUBOFromFrameContext(const FrameContext* ctx)
{
    YA_CORE_ASSERT(ctx, "FrameContext is null when filling light UBO");

    uLight.hasDirectionalLight = ctx->bHasDirectionalLight;
    if (ctx->bHasDirectionalLight) {
        uLight.dirLight.direction              = ctx->directionalLight.direction;
        uLight.dirLight.ambient                = ctx->directionalLight.ambient;
        uLight.dirLight.diffuse                = ctx->directionalLight.diffuse;
        uLight.dirLight.specular               = ctx->directionalLight.specular;
        uLight.dirLight.directionalLightMatrix = ctx->directionalLight.viewProjection;
    }

    uLight.numPointLights = ctx->numPointLights;
    for (uint32_t i = 0; i < ctx->numPointLights; ++i) {
        const auto& pl = ctx->pointLights[i];
        auto&       dst = uLight.pointLights[i];
        dst            = {};
        dst.type        = pl.type;
        dst.constant    = pl.constant;
        dst.linear      = pl.linear;
        dst.quadratic   = pl.quadratic;
        dst.position    = pl.position;
        dst.farPlane    = pl.farPlane;
        dst.ambient     = pl.ambient;
        dst.diffuse     = pl.diffuse;
        dst.specular    = pl.specular;
        dst.spotDir     = pl.spotDir;
        dst.innerCutOff = pl.innerCutOff;
        dst.outerCutOff = pl.outerCutOff;
    }
}

// MARK: grab resources
void PhongMaterialSystem::preTick(float deltaTime, const FrameContext* ctx)
{
    YA_PROFILE_FUNCTION();
    (void)deltaTime;
    (void)ctx;

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

    YA_CORE_ASSERT(_currentScenePassResources, "PhongMaterialSystem requires explicit scene pass resources before render");

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

    cmdBuf->bindPipeline(_pipeline.get());

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
        if (App::get()->isMirrorRenderingEnabled()) {
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
        cmdBuf->bindDescriptorSets(_pipelineLayout.get(),
                                   0,
                                   {
                                       _currentScenePassResources->frameDS,
                                       resourceDS,
                                       paramDS,
                                       skyBoxCubeMapDS,
                                       depthBufferDS,
                                   });

        // update push constant
        PhongMaterialSystem::ModelPushConstant pushConst{
            .modelMat = tc->getTransform(),
        };
        cmdBuf->pushConstants(_pipelineLayout.get(),
                              _pipelineLayoutDesc.pushConstants[0].stageFlags,
                              0,
                              sizeof(PhongMaterialSystem::ModelPushConstant),
                              &pushConst);

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
}

void PhongMaterialSystem::onRenderGUI()
{
    using namespace ImGui;
    IMaterialSystem::onRenderGUI();
    TextColored(ImColor(1.0f, 1.0f, 0.0f, 1.0f),
                "scene pass: %s",
                _currentScenePassResources ? _currentScenePassResources->label.c_str() : "<unbound>");

    // if (TreeNode("Directional Light")) {
    //     ya::RenderContext ctx;
    //     // ya::renderReflectedType("DirectionalLight", ya::type_index_v<PhongMaterialSystem::DirectionalLightData>, &uLight.dirLight, ctx);
    //     TreePop();
    // }


    if (ImGui::TreeNode("Debug Options")) {
        bool bDebugNormal = (uDebug.bDebugNormal != 0);
        bool bDebugDepth  = (uDebug.bDebugDepth != 0);
        bool bDebugUV     = (uDebug.bDebugUV != 0);

        if (ImGui::Checkbox("Debug Normal", &bDebugNormal)) {
            uDebug.bDebugNormal = bDebugNormal ? 1u : 0u;
        }
        if (ImGui::Checkbox("Debug Depth", &bDebugDepth)) {
            uDebug.bDebugDepth = bDebugDepth ? 1u : 0u;
        }
        if (ImGui::Checkbox("Debug UV", &bDebugUV)) {
            uDebug.bDebugUV = bDebugUV ? 1u : 0u;
        }
        ImGui::DragFloat4("Float Param", glm::value_ptr(uDebug.floatParam), 0.1f);
        TreePop();
    }
}

void PhongMaterialSystem::setDirectionalShadowMappingEnabled(bool enabled)
{
    if (_bDirectionalShadowMapping == enabled) {
        return;
    }
    _bDirectionalShadowMapping       = enabled;
    _pipelineDesc.shaderDesc.defines = buildPhongShaderDefines(_bDirectionalShadowMapping);
    _pipeline->updateDesc(_pipelineDesc);
}


// TODO: descriptor set can be shared if they use same layout and data
void PhongMaterialSystem::updateFrameDS(const FrameContext* ctx)
{
    YA_PROFILE_FUNCTION();

    YA_CORE_ASSERT(_currentScenePassResources, "PhongMaterialSystem frame resources are not bound");

    auto app    = getApp();

    // Use passed camera context
    FrameUBO uFrame{};
    uFrame.projMat    = ctx->projection;
    uFrame.viewMat    = ctx->view;
    uFrame.resolution = glm::ivec2(ctx->extent.width, ctx->extent.height);
    uFrame.frameIdx   = app->getFrameIndex();
    uFrame.time       = (float)app->getElapsedTimeMS() / 1000.0f;
    uFrame.cameraPos  = ctx->cameraPos;

    // TODO: handle the rotation  of radians in shader
    // auto light               = uLight;
    // light.dirLight.direction = glm::radians(light.dirLight.direction);

    _currentScenePassResources->frameUBO->writeData(&uFrame, sizeof(FrameUBO), 0);
    _currentScenePassResources->debugUBO->writeData(&uDebug, sizeof(DebugUBO), 0);
}

void PhongMaterialSystem::updateMaterialParamDS(DescriptorSetHandle ds, PhongMaterialComponent& component, bool bOverrideDiffuse, bool bRecreated)
{
    YA_PROFILE_FUNCTION();
    (void)bOverrideDiffuse;
    (void)bRecreated;

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

    auto& normalTextureParam = params.textureParams[PhongMaterial::EResource::NormalTexture];
    if (auto* slot = component.getTextureSlot(PhongMaterial::EResource::NormalTexture); slot && slot->isValid()) {
        normalTextureParam.uvTransform = FMath::build_transform_mat3(
            slot->uvOffset,
            slot->uvRotation,
            slot->uvScale);
        normalTextureParam.bEnable = slot->bEnable;
    }
    else {
        normalTextureParam.bEnable = false;
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
    auto normalTV     = material->getTextureView(PhongMaterial::EResource::NormalTexture);

    DescriptorImageInfo diffuseTexture    = getDescriptorImageInfo(diffuseTV);
    DescriptorImageInfo specularTexture   = getDescriptorImageInfo(specularTV);
    DescriptorImageInfo reflectionTexture = getDescriptorImageInfo(reflectionTV);
    DescriptorImageInfo normalTexture     = getDescriptorImageInfo(normalTV);
    //  mirror or other rt?
    if (bOverrideDiffuse && App::get()->hasMirrorRenderResult()) {
        auto* mirrorRT      = App::get()->getMirrorRenderTarget();
        auto  mirrorTexture = mirrorRT ? mirrorRT->getCurFrameBuffer()->getColorTexture(0) : nullptr;
        if (mirrorTexture) {
            diffuseTexture = getDescriptorImageInfo(mirrorTexture->getImageView(), nullptr);
        }
    }

    render
        ->getDescriptorHelper()
        ->updateDescriptorSets(
            {
                IDescriptorSetHelper::genImageWrite(ds, 0, 0, EPipelineDescriptorType::CombinedImageSampler, {diffuseTexture}),
                IDescriptorSetHelper::genImageWrite(ds, 1, 0, EPipelineDescriptorType::CombinedImageSampler, {specularTexture}),
                IDescriptorSetHelper::genImageWrite(ds, 2, 0, EPipelineDescriptorType::CombinedImageSampler, {reflectionTexture}),
                IDescriptorSetHelper::genImageWrite(ds, 3, 0, EPipelineDescriptorType::CombinedImageSampler, {normalTexture}),
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
    const uint32_t textureCount = _pipelineLayoutDesc.descriptorSetLayouts[1].bindings.size();

    DescriptorPoolCreateInfo poolCI{
        .maxSets   = newDescriptorSetCount * (1 + textureCount),
        .poolSizes = {
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::UniformBuffer,
                .descriptorCount = newDescriptorSetCount,
            },
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::CombinedImageSampler,
                .descriptorCount = newDescriptorSetCount * textureCount, // tex0 + tex1  + tex2 for each material param in one set
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
