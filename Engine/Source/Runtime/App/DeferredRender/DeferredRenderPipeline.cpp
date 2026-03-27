#include "DeferredRenderPipeline.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "Runtime/App/App.h"

#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"


#include "Render/Core/TextureFactory.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Material/PhongMaterial.h"


#include "Resource/PrimitiveMeshCache.h"
#include "Resource/TextureLibrary.h"

#include <glm/gtx/string_cast.hpp>

namespace ya

{

void DeferredRenderPipeline::tick(const TickDesc& desc)
{
    const auto& cmdBuf = desc.cmdBuf;
    cmdBuf->debugBeginLabel("Deferred Pipeline");

    bool bViewPortRectValid = desc.viewportRect.extent.x > 0 && desc.viewportRect.extent.y > 0;
    if (!bViewPortRectValid) {
        return;
    }


    _gBufferPipeline->beginFrame();
    _lightPipeline->beginFrame();
    if (_skyboxSystem) {
        _skyboxSystem->beginFrame();
    }
    _postProcessStage.beginFrame();

    // Ensure render targets are up-to-date before rendering.
    // GBuffer pass uses renderTarget mode (auto-flushes in beginRendering),
    // but light pass uses manual images mode, so _viewportRT must be flushed
    // explicitly here (e.g. after viewport resize).
    _viewportRT->flushDirty();
    _gBufferRT->flushDirty();

    auto app   = App::get();
    auto scene = app->getSceneManager()->getActiveScene();
    if (!scene) {
        return;
    }

    // MARK: ressource upload
    auto drawListView = scene->getRegistry().view<MeshComponent, TransformComponent, PhongMaterialComponent>();

    // Prepare all material descriptor sets before issuing any draw calls.
    // This avoids updating descriptor sets after they have already been bound in the same command buffer.
    uint32_t         materialCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<PhongMaterial>());
    bool             force         = _matPool.ensureCapacity(materialCount);
    std::vector<int> preparedMaterial(materialCount, 0);

    for (const auto& [entity, mc, tc, pmc] : drawListView.each()) {
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

    // int numPointLights = 0;
    // for (const auto& [et, plc, tc] :
    //      scene->getRegistry().view<PointLightComponent, TransformComponent>().each()) {
    //     if (numPointLights >= MAX_POINT_LIGHTS) {
    //         break;
    //     }
    //     ++numPointLights;
    // }

    for (const auto& [et, dlc, tc] :
         scene->getRegistry().view<DirectionalLightComponent, TransformComponent>().each())
    {
        // ctx.bHasDirectionalLight             = true; // TODO: invoke a macro to disable dir light  branch
        uLightPassLightData.dirLight.dir     = tc.getForward();
        uLightPassLightData.dirLight.color   = dlc._color;
        uLightPassLightData.dirLight.ambient = dlc._ambient;
    }

    uLightPassFrameData.viewPos    = desc.cameraPos;
    uLightPassFrameData.projMatrix = desc.projection;
    uLightPassFrameData.viewMatrix = desc.view;
    _frameUBO->writeData(&uLightPassFrameData, sizeof(LightPassFrameData), 0);
    _frameUBO->flush();

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
            ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
        },
        .depthClearValue = ClearValue(1.0f, 0),
        .renderTarget    = _gBufferRT.get(),
    };
    cmdBuf->beginRendering(gBufferRI);


    cmdBuf->bindPipeline(_gBufferPipeline.get());
    auto pl = _gBufferPPL;

    float gBufferViewportY      = 0.0f;
    float gBufferViewportHeight = static_cast<float>(viewportHeight);
    if (_bReverseViewportY) {
        gBufferViewportY      = static_cast<float>(viewportHeight);
        gBufferViewportHeight = -static_cast<float>(viewportHeight);
    }
    cmdBuf->setViewport(0.0f,
                        gBufferViewportY,
                        static_cast<float>(viewportWidth),
                        gBufferViewportHeight);
    cmdBuf->setScissor(0, 0, viewportWidth, viewportHeight);

    for (const auto& [entity, mc, tc, pmc] : drawListView.each()) {
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
        .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 0.0f)},
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

    float lightVpY      = 0.0f;
    float lightVpHeight = static_cast<float>(viewportHeight);
    // if (_bReverseViewportY) {
    //     lightVpY      = static_cast<float>(viewportHeight);
    //     lightVpHeight = -static_cast<float>(viewportHeight);
    // }
    cmdBuf->setViewport(0.0f, lightVpY, static_cast<float>(viewportWidth), lightVpHeight);
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

    // MARK: Skybox (after light pass, reuses GBuffer depth)
    if (_skyboxSystem && _skyboxSystem->bEnabled) {
        cmdBuf->debugBeginLabel("Deferred Skybox");
        FrameContext skyboxCtx;
        skyboxCtx.view       = desc.view;
        skyboxCtx.projection = desc.projection;
        skyboxCtx.cameraPos  = desc.cameraPos;
        skyboxCtx.extent     = Extent2D{
                .width  = viewportWidth,
                .height = viewportHeight,
        };
        _skyboxSystem->tick(cmdBuf, desc.dt, &skyboxCtx);
        cmdBuf->debugEndLabel();
    }

    // Light pass left open for App-level 2D rendering; App calls endViewportPass() after Render2D.
    _viewportRI = lightPassRI;

    // Save context for postprocessing in endViewportPass()
    _lastTickCtx.view       = desc.view;
    _lastTickCtx.projection = desc.projection;
    _lastTickCtx.cameraPos  = desc.cameraPos;
    _lastTickCtx.extent     = Extent2D{
            .width  = viewportWidth,
            .height = viewportHeight,
    };
    _lastTickDesc = desc;
#pragma endregion

    cmdBuf->debugEndLabel();
}


void DeferredRenderPipeline::endViewportPass(ICommandBuffer* cmdBuf)
{
    // Close light pass rendering (opened in tick())
    cmdBuf->endRendering(_viewportRI);

    // Determine source color texture from light pass output
    auto* inputTexture = _viewportRT->getCurFrameBuffer()->getColorTexture(0);

    // Run postprocessing via shared stage; returns input unchanged if disabled
    viewportTexture = _postProcessStage.execute(cmdBuf,
                                                inputTexture,
                                                _lastTickDesc.viewportRect.extent,
                                                _lastTickDesc.dt,
                                                &_lastTickCtx);
}

void DeferredRenderPipeline::ensureDebugSwizzledViews()
{
    auto gBuf2 = _gBufferRT->getCurFrameBuffer()->getColorTexture(2);
    if (!gBuf2) return;

    auto* iv = gBuf2->getImageView();
    if (!iv) return;

    // Check if source image view changed (e.g. after resize/recreate)
    if (_cachedAlbedoSpecImageViewHandle == iv->getHandle() &&
        _debugAlbedoRGBView && _debugSpecularAlphaView) {
        return; // Already up-to-date
    }

    auto* factory = ITextureFactory::get();
    if (!factory) return;

    auto image = gBuf2->getImageShared();

    // Create RGB-only view (alpha forced to 1)
    {
        ImageViewCreateInfo ci{
            .label       = "GBuffer AlbedoSpec RGB View",
            .viewType    = EImageViewType::View2D,
            .aspectFlags = EImageAspect::Color,
            .components  = ComponentMapping::RGBOnly(),
        };
        _debugAlbedoRGBView = factory->createImageView(image, ci);
        if (_debugAlbedoRGBView) {
            _debugAlbedoRGBView->setDebugName("GBuffer_AlbedoSpec_RGB");
        }
    }

    // Create alpha-as-grayscale view (A → R,G,B; alpha = 1)
    {
        ImageViewCreateInfo ci{
            .label       = "GBuffer AlbedoSpec Alpha View",
            .viewType    = EImageViewType::View2D,
            .aspectFlags = EImageAspect::Color,
            .components  = ComponentMapping::AlphaToGrayscale(),
        };
        _debugSpecularAlphaView = factory->createImageView(image, ci);
        if (_debugSpecularAlphaView) {
            _debugSpecularAlphaView->setDebugName("GBuffer_AlbedoSpec_Alpha");
        }
    }

    _cachedAlbedoSpecImageViewHandle = iv->getHandle();
    YA_CORE_INFO("Created debug swizzled image views for GBuffer albedoSpecular");
}

void DeferredRenderPipeline::renderGUI()
{
    if (!ImGui::CollapsingHeader("Deferrer Pipeline")) {
        return;
    }
    ImGui::Checkbox("Reverse Viewport Y", &_bReverseViewportY);
    _gBufferPipeline->renderGUI();
    _lightPipeline->renderGUI();
    if (_skyboxSystem) {
        _skyboxSystem->renderGUI();
    }
    _postProcessStage.renderGUI();
}

} // namespace ya
