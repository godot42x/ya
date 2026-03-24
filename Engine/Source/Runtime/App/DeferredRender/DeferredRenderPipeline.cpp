#include "DeferredRenderPipeline.h"
#include "Runtime/App/App.h"

#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"


#include "Core/Math/Math.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Material/PhongMaterial.h"
#include "Resource/TextureLibrary.h"

#include <imgui.h>
namespace ya

{

void DeferredRenderPipeline::tick(const TickDesc& desc)
{
    const auto& cmdBuf = desc.cmdBuf;

    _gBufferPipeline->beginFrame();

    auto app   = App::get();
    auto scene = app->getSceneManager()->getActiveScene();
    if (!scene) {
        return;
    }

#pragma region GBuffer
    // 1. grab all mesh with * material, render to geometry
    RenderingInfo gBufferRI{
        .label      = "GBuffer Pass",
        .renderArea = Rect2D{
            .pos    = {0, 0},
            .extent = _gBufferRT->getExtent().toVec2(),
        },
        .layerCount       = 1,
        .colorClearValues = {
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
        },
        .depthClearValue = ClearValue(1.0f, 0),
        .renderTarget    = _gBufferRT.get(),
    };
    cmdBuf->beginRendering(gBufferRI);


    cmdBuf->bindPipeline(_gBufferPipeline.get());
    // cmdBuf->bindDescriptorSets()
    using PC = slang_types::DeferredRender::GBufferPass::PushConstants;
    auto pl  = _gBufferPPL;

    uint32_t materialCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<PhongMaterial>());
    bool     force         = _matPool.ensureCapacity(materialCount);

    // flush each material and write to GBuffer
    // 1. params UBO
    // 2. each slot's texture
    // Then update other global resource(Not for each material) to gpu, eg: FrameUBO, LightUBO
    auto view = scene->getRegistry().view<MeshComponent, TransformComponent, PhongMaterialComponent>();
    for (const auto& [entity, mc, tc, pmc] : view.each())
    {
        PhongMaterial* material = pmc.getMaterial();
        if (!material || material->getIndex() < 0)
            continue;

        uint32_t idx = static_cast<uint32_t>(material->getIndex());

        _matPool.flushDirty(
            material,
            force,
            [](IBuffer* ubo, PhongMaterial* mat) {
                using PD = slang_types::DeferredRender::GBufferPass::ParamsData;
                PD          params{};
                const auto& src = mat->getParams().textureParams;
                for (int i = 0; i < PhongMaterial::EResource::Count; ++i) {
                    params.textures[i].bEnable     = src[i].bEnable;
                    params.textures[i].uvTransform = src[i].uvTransform;
                }
                ubo->writeData(&params, sizeof(PD), 0);
            },
            [this](DescriptorSetHandle ds, PhongMaterial* mat) {
                // Use TextureBinding handles directly (with fallback built-in)
                auto getImageInfo = [](const TextureBinding& tb) -> DescriptorImageInfo {
                    return DescriptorImageInfo(tb.getImageViewHandle(), tb.getSamplerHandle(), EImageLayout::ShaderReadOnlyOptimal);
                };
                _render->getDescriptorHelper()->updateDescriptorSets(
                    {
                        IDescriptorSetHelper::genImageWrite(ds, 0, 0, EPipelineDescriptorType::CombinedImageSampler,
                                                           {getImageInfo(mat->getTextureBinding(PhongMaterial::EResource::DiffuseTexture))}),
                        IDescriptorSetHelper::genImageWrite(ds, 1, 0, EPipelineDescriptorType::CombinedImageSampler,
                                                           {getImageInfo(mat->getTextureBinding(PhongMaterial::EResource::SpecularTexture))}),
                        IDescriptorSetHelper::genImageWrite(ds, 2, 0, EPipelineDescriptorType::CombinedImageSampler,
                                                           {getImageInfo(mat->getTextureBinding(PhongMaterial::EResource::ReflectionTexture))}),
                        IDescriptorSetHelper::genImageWrite(ds, 3, 0, EPipelineDescriptorType::CombinedImageSampler,
                                                           {getImageInfo(mat->getTextureBinding(PhongMaterial::EResource::NormalTexture))}),
                    },
                    {}); });

        // set 1 = resource (textures), set 2 = params UBO
        cmdBuf->bindDescriptorSets(pl.get(), 1, {_matPool.resourceDS(idx), _matPool.paramDS(idx)});

        PC pc{
            .modelMat = tc.getTransform(),
        };
        cmdBuf->pushConstants(pl.get(),
                              EShaderStage::Vertex,
                              0,
                              sizeof(PC),
                              &pc);
        mc.getMesh()->draw(cmdBuf);
    }


    cmdBuf->endRendering(gBufferRI);
#pragma endregion


    RenderingInfo::ImageSpec sharedDepth{
        .texture       = _gBufferRT->getCurFrameBuffer()->getDepthTexture(),
        .initialLayout = EImageLayout::DepthStencilAttachmentOptimal,
        .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
    };

    RenderingInfo lightPassRI{
        .label      = "Light Pass",
        .renderArea = {
            .pos    = {0, 0},
            .extent = _viewportRT->getExtent().toVec2(),
        },
        .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 1.0f)},
        .colorAttachments = {
            RenderingInfo::ImageSpec{
                .texture       = _viewportRT->getCurFrameBuffer()->getColorTexture(0),
                .initialLayout = EImageLayout::ColorAttachmentOptimal,
                .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
            },
        },
        .depthAttachment = &sharedDepth,
    };

    cmdBuf->beginRendering(lightPassRI);

    // Fill light pass frame data from TickDesc
    LightPassFrameData frameData{};
    frameData.viewPos    = desc.cameraPos;
    frameData.viewMatrix = desc.view;
    frameData.projMatrix = desc.projection;

    // Collect light data from scene into deferred light UBO
    FrameContext ctx{};
    ctx.view       = desc.view;
    ctx.projection = desc.projection;
    ctx.cameraPos  = desc.cameraPos;

    ctx.numPointLights = 0;
    for (const auto& [et, plc, tc] :
         scene->getRegistry().view<PointLightComponent, TransformComponent>().each())
    {
        if (ctx.numPointLights >= MAX_POINT_LIGHTS) break; // FrameContext::pointLights is sized to MAX_POINT_LIGHTS
        auto& pl     = ctx.pointLights[ctx.numPointLights];
        pl.type      = static_cast<float>(plc._type);
        pl.constant  = plc._constant;
        pl.linear    = plc._linear;
        pl.quadratic = plc._quadratic;
        pl.position  = tc._position;
        pl.ambient   = plc._ambient;
        pl.diffuse   = plc._diffuse;
        pl.specular  = plc._specular;
        ++ctx.numPointLights;
    }

    for (const auto& [et, dlc, tc] :
         scene->getRegistry().view<PointLightComponent, TransformComponent>().each())
    {
        ctx.bHasDirectionalLight       = true;
        ctx.directionalLight.direction = tc.getForward(),
        ctx.directionalLight.ambient   = dlc._ambient;
        ctx.directionalLight.diffuse   = dlc._diffuse;
        ctx.directionalLight.specular  = dlc._specular;
    }


    fillLightDataFromFrameContext(&ctx);

    // TODO: bind frameData UBO, uLightPassLightData UBO, GBuffer textures, then draw fullscreen quad
    cmdBuf->bindPipeline(_lightPipeline.get());
    // cmdBuf->bindDescriptorSets(
    //     _lightPipeline.get(),
    //     0,
    //     {
    //         _currentScenePassResources->frameDS,   // frame data UBO
    //         _currentScenePassResources->lightDS,   // light data UBO
    //         _currentScenePassResources->gBufferDS, // GBuffer textures
    //     });

    cmdBuf->endRendering(lightPassRI);
#pragma endregion
}


void DeferredRenderPipeline::fillLightDataFromFrameContext(const FrameContext* ctx)
{
    YA_CORE_ASSERT(ctx, "FrameContext is null when filling deferred light data");

    // uLightPassLightData.numPointLights = std::min(ctx->numPointLights, MAX_POINT_LIGHTS_DEFERRED);
    // for (uint32_t i = 0; i < uLightPassLightData.numPointLights; ++i) {
    //     const auto& src = ctx->pointLights[i];
    //     auto&       dst = uLightPassLightData.pointLights[i];
    //     dst.pos         = src.position;
    //     dst.color       = src.diffuse; // Use diffuse as the light color for deferred
    // }
    uLightPassLightData.dirLight.color     = ctx->directionalLight.diffuse;
    uLightPassLightData.dirLight.ambient   = ctx->directionalLight.ambient;
    uLightPassLightData.dirLight.shininess = 32;
}

void DeferredRenderPipeline::renderGUI()
{
    if (!ImGui::CollapsingHeader("GBuffer Pipeline")) {
        return;
    }
    _gBufferPipeline->renderGUI();
}

} // namespace ya
