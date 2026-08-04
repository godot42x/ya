#include "BasicShadowMapTechnique.h"

#include "Core/Profiling/Instrumentor.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/RenderGraphImportUtils.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"

#include "Core/Math/Math.h"
#include <format>

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════════
// Init / Destroy
// ═══════════════════════════════════════════════════════════════════════════

void BasicShadowMapTechnique::init(IRender* render, const ShadowSettings& settings)
{
    _render   = render;
    _settings = settings;
    _shadowExtent = {.width = settings.resolution, .height = settings.resolution};
    _standaloneGraphExecutor = std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory());

    _frameResources.init(render);
    _directionalPass.init(render, _shadowExtent, _frameResources, _standaloneGraphExecutor.get());
    _pointPass.init(render, _shadowExtent, _frameResources, _standaloneGraphExecutor.get());
}

void BasicShadowMapTechnique::destroy()
{
    _directionalPass.destroy();
    _pointPass.destroy();
    _standaloneGraphExecutor.reset();
    _frameResources.destroy();
    _depthImage.reset();
    _shadowDepthArrayView.reset();
    _render      = nullptr;
}

void BasicShadowMapTechnique::applySettings(const ShadowSettings& settings)
{
    _settings = settings;
    // TODO: if resolution changed, rebuild render target and textures
}

// ═══════════════════════════════════════════════════════════════════════════
// Prepare / Execute
// ═══════════════════════════════════════════════════════════════════════════

void BasicShadowMapTechnique::prepare(uint32_t flightIndex, const RenderFrameData& frameData)
{
    YA_PROFILE_FUNCTION();
    if (!_settings.isEnabled()) return;

    const auto payload = buildFramePayload(flightIndex, frameData);
    _lastPreparedPointLightCount = payload.pointLightCount;

    if (!_frameResources.prepare(payload)) {
        YA_CORE_ERROR("BasicShadowMapTechnique failed to prepare shadow frame resources");
        return;
    }

    if (payload.directionalEnabled()) {
        _directionalPass.prepare(payload);
    }
    if (payload.pointEnabled()) {
        _pointPass.prepare(payload);
    }
}

void BasicShadowMapTechnique::execute(ICommandBuffer* cmdBuf, uint32_t flightIndex, const RenderFrameData& frameData)
{
    YA_PROFILE_FUNCTION();
    if (!_settings.isEnabled()) return;

    auto payload = buildFramePayload(flightIndex, frameData);
    payload.pointLightCount = std::min(_lastPreparedPointLightCount, static_cast<uint32_t>(MAX_POINT_LIGHTS));

    cmdBuf->debugBeginLabel("BasicShadowMap");

    if (payload.directionalEnabled()) {
        _directionalPass.execute(cmdBuf, payload);
    }
    if (payload.pointEnabled()) {
        _pointPass.execute(cmdBuf, payload);
    }

    cmdBuf->debugEndLabel();
}

ShadowGraphOutputs BasicShadowMapTechnique::appendGraphPasses(
    RenderGraph& graph,
    uint32_t flightIndex,
    const RenderFrameData& frameData)
{
    ShadowGraphOutputs outputs{};
    if (!_settings.isEnabled()) return outputs;

    auto payload = buildFramePayload(flightIndex, frameData);
    payload.pointLightCount = std::min(_lastPreparedPointLightCount, static_cast<uint32_t>(MAX_POINT_LIGHTS));

    if (_depthImage && _shadowDepthArrayView) {
        outputs.shadowDepth = graph.importTexture(makeImportedTextureDesc(
            _depthImage,
            _shadowDepthArrayView,
            "BasicShadowMap.Depth",
            EImageLayout::ShaderReadOnlyOptimal,
            EImageUsage::Sampled));
    }

    if (payload.directionalEnabled()) {
        outputs.lastPass = _directionalPass.appendGraphPasses(graph, payload, outputs.lastPass);
    }
    if (payload.pointEnabled()) {
        outputs.lastPass = _pointPass.appendGraphPasses(graph, payload, outputs.lastPass);
    }
    return outputs;
}

// ═══════════════════════════════════════════════════════════════════════════
// Render target / texture management
// ═══════════════════════════════════════════════════════════════════════════

void BasicShadowMapTechnique::refreshShadowResources(const std::shared_ptr<IImage>& depthImage, EFormat::T depthFormat, Extent2D shadowExtent)
{
    if (!_render || !depthImage) return;

    _depthImage   = depthImage;
    _shadowExtent = shadowExtent;
    _directionalPass.setShadowExtent(_shadowExtent);
    _pointPass.setShadowExtent(_shadowExtent);

    rebuildLayerTextures(depthImage);
    _directionalPass.refreshPipeline(depthFormat);
    _pointPass.refreshPipeline(depthFormat);
}

void BasicShadowMapTechnique::rebuildLayerTextures(const std::shared_ptr<IImage>& shadowImage)
{
    if (!_render || !shadowImage) return;

    // Directional cascades: reserved layers 0..MAX_DIRECTIONAL_CASCADES-1.
    auto* resourceFactory = _render->getResourceFactory();
    _shadowDepthArrayView = resourceFactory->createImageView(
        shadowImage,
        ImageViewCreateInfo{
            .label          = "BasicShadowMap_ShadowDepthArrayView",
            .viewType       = EImageViewType::View2DArray,
            .aspectFlags    = EImageAspect::Depth,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = getShadowTotalLayerCount(),
        });
    std::array<stdptr<IImageView>, MAX_DIRECTIONAL_CASCADES> directionalViews{};
    for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_DIRECTIONAL_CASCADES; ++cascadeIndex) {
        directionalViews[cascadeIndex] = resourceFactory->createImageView(
            shadowImage,
            ImageViewCreateInfo{
                .label          = std::format("BasicShadowMap_DirectionalDepthView_{}", cascadeIndex),
                .viewType       = EImageViewType::View2D,
                .aspectFlags    = EImageAspect::Depth,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = cascadeIndex,
                .layerCount     = 1,
            });
    }
    _directionalPass.setDepthAttachments(shadowImage, std::move(directionalViews));

    // Point faces: layers 6..41.
    _pointPass.rebuildFaceTextures(shadowImage);
}

// ═══════════════════════════════════════════════════════════════════════════
// GUI
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// Payload / Point shadow matrix calculation
// ═══════════════════════════════════════════════════════════════════════════

BasicShadowFramePayload BasicShadowMapTechnique::buildFramePayload(uint32_t flightIndex, const RenderFrameData& frameData) const
{
    YA_PROFILE_FUNCTION();
    const uint32_t pointLightCount = std::min(
        frameData.numPointLights,
        _settings.getEffectivePointLightCount());

    BasicShadowFramePayload payload{
        .flightIndex = flightIndex,
        .frameData   = &frameData,
        .settings    = &_settings,
        .frameUBO    = FrameUBO{
            .directionalLightMatrix = frameData.directionalLight.viewProjection,
            .numPointLights         = pointLightCount,
            .hasDirectionalLight    = frameData.bHasDirectionalLight ? 1u : 0u,
        },
        .pointLightCount = pointLightCount,
    };
    populatePointShadowMatrices(frameData, payload.frameUBO, pointLightCount);
    return payload;
}

void BasicShadowMapTechnique::populatePointShadowMatrices(const RenderFrameData& frameData, FrameUBO& frameDataUBO, uint32_t pointLightCount) const
{
    YA_PROFILE_FUNCTION();
    for (uint32_t i = 0; i < pointLightCount; ++i) {
        const auto&      pointLight = frameData.pointLights[i];
        const glm::vec3& pos        = pointLight.position;
        const glm::mat4  faceProj   = FMath::perspective(glm::radians(90.0f), 1.0f, pointLight.nearPlane, pointLight.farPlane);

        for (int face = ECubeFace::CubeFace_PosX; face < ECubeFace::CubeFace_Count; ++face) {
            glm::mat4       view{};
            const glm::vec3 down     = glm::vec3{0, -1, 0};
            const glm::vec3 backward = {0, 0, 1};

            if constexpr (FMath::Vector::IsRightHanded) {
                switch ((ECubeFace)face) {
                case CubeFace_PosX: view = FMath::lookAt(pos, pos + glm::vec3(1, 0, 0), down); break;
                case CubeFace_NegX: view = FMath::lookAt(pos, pos + glm::vec3(-1, 0, 0), down); break;
                case CubeFace_PosY: view = FMath::lookAt(pos, pos + glm::vec3(0, 1, 0), backward); break;
                case CubeFace_NegY: view = FMath::lookAt(pos, pos + glm::vec3(0, -1, 0), -backward); break;
                case CubeFace_PosZ: view = FMath::lookAt(pos, pos + glm::vec3(0, 0, 1), down); break;
                case CubeFace_NegZ: view = FMath::lookAt(pos, pos + glm::vec3(0, 0, -1), down); break;
                case CubeFace_Count: UNREACHABLE();
                }
            }

            frameDataUBO.pointLights[i].matrix[face] = faceProj * view;
            frameDataUBO.pointLights[i].pos          = pos;
            frameDataUBO.pointLights[i].farPlane     = pointLight.farPlane;
        }
    }
}

} // namespace ya
