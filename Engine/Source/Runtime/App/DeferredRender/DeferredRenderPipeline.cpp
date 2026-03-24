#include "DeferredRenderPipeline.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "Runtime/App/App.h"

#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"


#include "Render/Material/MaterialFactory.h"
#include "Render/Material/PhongMaterial.h"

#include "Resource/PrimitiveMeshCache.h"

namespace ya

{

void DeferredRenderPipeline::tick(const TickDesc& desc)
{
    const auto& cmdBuf = desc.cmdBuf;

    bool bViewPortRectValid = desc.viewportRect.extent.x > 0 && desc.viewportRect.extent.y > 0;
    if (!bViewPortRectValid)
    {
        return;
    }


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
    auto pl = _gBufferPPL;
    cmdBuf->setViewport(0, 0, desc.viewportRect.extent.x, desc.viewportRect.extent.y);
    cmdBuf->setScissor(0, 0, desc.viewportRect.extent.x, desc.viewportRect.extent.y);

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
                _render ->getDescriptorHelper()->updateDescriptorSets( {
                        IDescriptorSetHelper::writeOneImage( ds, 0, mat->getTextureBinding(PhongMaterial::EResource::DiffuseTexture)),
                        IDescriptorSetHelper::writeOneImage(ds, 1,  mat->getTextureBinding(PhongMaterial::EResource::SpecularTexture)),
                        IDescriptorSetHelper::writeOneImage(ds, 2,  mat->getTextureBinding(PhongMaterial::EResource::NormalTexture)),
                    },
                    {}); });

        // set 1 = resource (textures), set 2 = params UBO
        cmdBuf->bindDescriptorSets(
            pl.get(),
            1,
            {
                _matPool.resourceDS(idx),
                _matPool.paramDS(idx),
            });

        GBufferPushConstant pc{
            .modelMat = tc.getTransform(),
        };
        cmdBuf->pushConstants(pl.get(),
                              EShaderStage::Vertex,
                              0,
                              sizeof(GBufferPushConstant),
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
         scene->getRegistry().view<DirectionalLightComponent, TransformComponent>().each())
    {
        ctx.bHasDirectionalLight       = true;
        ctx.directionalLight.direction = tc.getForward(),
        ctx.directionalLight.ambient   = dlc._ambient;
        ctx.directionalLight.diffuse   = dlc._diffuse;
        ctx.directionalLight.specular  = dlc._specular;
    }

    uLightPassFrameData.viewPos    = ctx.cameraPos;
    uLightPassFrameData.projMatrix = ctx.projection;
    uLightPassFrameData.viewMatrix = ctx.view;
    _frameUBO->writeData(&uLightPassFrameData, sizeof(uLightPassFrameData), 0);
    _frameUBO->flush();

    uLightPassLightData.dirLight.color     = ctx.directionalLight.diffuse;
    uLightPassLightData.dirLight.ambient   = ctx.directionalLight.ambient;
    uLightPassLightData.dirLight.shininess = 32;
    _lightUBO->writeData(&uLightPassLightData, sizeof(LightPassLightData), 0);
    _lightUBO->flush();

    // update 3 GBuffer textures to light pass, set 1 = resource (textures)
    auto sampler = TextureLibrary::get().getDefaultSampler();
    _render
        ->getDescriptorHelper()
        ->updateDescriptorSets(
            {
                IDescriptorSetHelper::writeOneImage(
                    _lightTexturesDS,
                    0,
                    _gBufferRT->getCurFrameBuffer()->getColorTexture(0)->getImageView(),
                    sampler.get()),
                IDescriptorSetHelper::writeOneImage(
                    _lightTexturesDS,
                    1,
                    _gBufferRT->getCurFrameBuffer()->getColorTexture(1)->getImageView(),
                    sampler.get()),
                IDescriptorSetHelper::writeOneImage(
                    _lightTexturesDS,
                    2,
                    _gBufferRT->getCurFrameBuffer()->getColorTexture(2)->getImageView(),
                    sampler.get()),
            });

    // TODO: bind frameData UBO, uLightPassLightData UBO, GBuffer textures, then draw fullscreen quad
    cmdBuf->bindPipeline(_lightPipeline.get());
    cmdBuf->setViewport(0, 0, desc.viewportRect.extent.x, desc.viewportRect.extent.y);
    cmdBuf->setScissor(0, 0, desc.viewportRect.extent.x, desc.viewportRect.extent.y);

    cmdBuf->bindDescriptorSets(
        _lightPPL.get(),
        0,
        {
            _frameAndLightDS,
            _lightTexturesDS,
        });

    auto quad = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Quad);
    quad->draw(cmdBuf);
    cmdBuf->endRendering(lightPassRI);
#pragma endregion
}


void DeferredRenderPipeline::renderGUI()
{
    if (!ImGui::CollapsingHeader("GBuffer Pipeline")) {
        return;
    }
    _gBufferPipeline->renderGUI();
}

} // namespace ya
