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
    cmdBuf->debugBeginLabel("Deferred Pipeline");

    bool bViewPortRectValid = desc.viewportRect.extent.x > 0 && desc.viewportRect.extent.y > 0;
    if (!bViewPortRectValid)
    {
        return;
    }


    _gBufferPipeline->beginFrame();
    _lightPipeline->beginFrame();

    auto app   = App::get();
    auto scene = app->getSceneManager()->getActiveScene();
    if (!scene) {
        return;
    }

    // MARK: ressource upload
    auto view = scene->getRegistry().view<MeshComponent, TransformComponent, PhongMaterialComponent>();

    // Prepare all material descriptor sets before issuing any draw calls.
    // This avoids updating descriptor sets after they have already been bound in the same command buffer.
    uint32_t         materialCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<PhongMaterial>());
    bool             force         = _matPool.ensureCapacity(materialCount);
    std::vector<int> preparedMaterial(materialCount, 0);

    for (const auto& [entity, mc, tc, pmc] : view.each())
    {
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
                _render->getDescriptorHelper()->updateDescriptorSets(
                    {
                        IDescriptorSetHelper::writeOneImage(ds, 0, mat->getTextureBinding(PhongMaterial::EResource::DiffuseTexture)),
                        IDescriptorSetHelper::writeOneImage(ds, 1, mat->getTextureBinding(PhongMaterial::EResource::SpecularTexture)),
                        IDescriptorSetHelper::writeOneImage(ds, 2, mat->getTextureBinding(PhongMaterial::EResource::NormalTexture)),
                    },
                    {});
            });

        preparedMaterial[idx] = 1;
    }

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
        ctx.directionalLight.direction = tc.getForward();
        ctx.directionalLight.ambient   = dlc._ambient;
        ctx.directionalLight.diffuse   = dlc._diffuse;
        ctx.directionalLight.specular  = dlc._specular;
    }

    uLightPassFrameData.viewPos    = ctx.cameraPos;
    uLightPassFrameData.projMatrix = ctx.projection;
    uLightPassFrameData.viewMatrix = ctx.view;
    _frameUBO->writeData(&uLightPassFrameData, sizeof(LightPassFrameData), 0);
    _frameUBO->flush();

    uLightPassLightData.dirLight.dir       = ctx.directionalLight.direction;
    uLightPassLightData.dirLight.color     = ctx.directionalLight.diffuse;
    uLightPassLightData.dirLight.ambient   = ctx.directionalLight.ambient;
    uLightPassLightData.dirLight.shininess = 32;
    _lightUBO->writeData(&uLightPassLightData, sizeof(LightPassLightData), 0);
    _lightUBO->flush();

#pragma region GBuffer
    const uint32_t viewportWidth  = static_cast<uint32_t>(desc.viewportRect.extent.x);
    const uint32_t viewportHeight = static_cast<uint32_t>(desc.viewportRect.extent.y);

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
    auto pl = _gBufferPPL;
    cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
    cmdBuf->setScissor(0, 0, viewportWidth, viewportHeight);

    for (const auto& [entity, mc, tc, pmc] : view.each())
    {
        PhongMaterial* material = pmc.getMaterial();
        if (!material || material->getIndex() < 0)
            continue;

        uint32_t idx = static_cast<uint32_t>(material->getIndex());

        // set 1 = resource (textures), set 2 = params UBO
        cmdBuf->bindDescriptorSets(
            pl.get(),
            0,
            {
                _frameAndLightDS,
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

#pragma region Lighting

    _sharedDepthSpec = RenderingInfo::ImageSpec{
        .texture       = _gBufferRT->getCurFrameBuffer()->getDepthTexture(),
        .initialLayout = EImageLayout::Undefined,
        .finalLayout   = EImageLayout::Undefined,
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
        .depthAttachment = &_sharedDepthSpec,
    };

    cmdBuf->beginRendering(lightPassRI);

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

    // Light pass left open for App-level 2D rendering; App calls endViewportPass() after Render2D.
    _viewportRI = lightPassRI;
#pragma endregion

    cmdBuf->debugEndLabel();
}


void DeferredRenderPipeline::endViewportPass(ICommandBuffer* cmdBuf)
{
    // Close light pass rendering (opened in tick())
    cmdBuf->endRendering(_viewportRI);
}

void DeferredRenderPipeline::renderGUI()
{
    if (!ImGui::CollapsingHeader("Deferrer Pipeline")) {
        return;
    }
    _gBufferPipeline->renderGUI();
    _lightPipeline->renderGUI();
}

} // namespace ya
