#include "DeferredRenderPipeline.h"
#include "Runtime/App/App.h"

#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/TransformComponent.h"

#include "Core/Math/Math.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Material/PhongMaterial.h"
#include "Resource/TextureLibrary.h"

#include <imgui.h>
namespace ya

{

void DeferredRenderPipeline::tick(ICommandBuffer* cmdBuf)
{
    _gBufferPipeline->beginFrame();

    auto app   = App::get();
    auto scene = app->getSceneManager()->getActiveScene();
    if (!scene) {
        return;
    }

    // 1. grab all mesh with * material, render to geometry
    cmdBuf->beginRendering(RenderingInfo{
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
    });


    cmdBuf->bindPipeline(_gBufferPipeline.get());
    // cmdBuf->bindDescriptorSets()
    using PC = slang_types::DeferredRender::GBufferPass::PushConstants;
    auto pl  = _gBufferPPL;

    uint32_t materialCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<PhongMaterial>());
    bool     force         = _matPool.ensureCapacity(materialCount);

    // flush material and write to GBuffer
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


    cmdBuf->endRendering({.renderTarget = _gBufferRT.get()});

    // cmdBuf->beginRendering()
}


void DeferredRenderPipeline::renderGUI()
{
    if (!ImGui::CollapsingHeader("GBuffer Pipeline")) {
        return;
    }
    _gBufferPipeline->renderGUI();
}

} // namespace ya
