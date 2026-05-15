#include "BasicShadowMapTechnique.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/Texture.h"
#include "Render/Core/TextureFactory.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"

#include "Core/Math/Math.h"
#include "imgui.h"

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

    _directionalPass.init(render, _shadowExtent);
    _pointPass.init(render, _shadowExtent);
    _pointPass.setUseIndirectDraw(settings.pointLightUseIndirect);
}

void BasicShadowMapTechnique::destroy()
{
    _directionalPass.destroy();
    _pointPass.destroy();
    _shadowMapRT = nullptr;
    _render      = nullptr;
}

void BasicShadowMapTechnique::applySettings(const ShadowSettings& settings)
{
    _settings = settings;
    _pointPass.setUseIndirectDraw(settings.pointLightUseIndirect);
    // TODO: if resolution changed, rebuild render target and textures
}

// ═══════════════════════════════════════════════════════════════════════════
// Prepare / Execute
// ═══════════════════════════════════════════════════════════════════════════

void BasicShadowMapTechnique::prepare(uint32_t flightIndex, const RenderFrameData& frameData)
{
    if (!_settings.isEnabled()) return;

    const auto payload = buildFramePayload(flightIndex, frameData);
    _lastPreparedPointLightCount = payload.pointLightCount;

    if (payload.directionalEnabled) {
        _directionalPass.prepare(payload);
    }
    if (payload.pointEnabled) {
        _pointPass.prepare(payload);
    }
}

void BasicShadowMapTechnique::execute(ICommandBuffer* cmdBuf, uint32_t flightIndex, const RenderFrameData& frameData)
{
    if (!_settings.isEnabled()) return;

    auto payload = buildFramePayload(flightIndex, frameData);
    payload.pointLightCount = std::min(_lastPreparedPointLightCount, static_cast<uint32_t>(MAX_POINT_LIGHTS));

    cmdBuf->debugBeginLabel("BasicShadowMap");

    if (payload.directionalEnabled) {
        _directionalPass.execute(cmdBuf, payload);
    }
    if (payload.pointEnabled) {
        _pointPass.execute(cmdBuf, payload);
    }

    cmdBuf->debugEndLabel();
}

// ═══════════════════════════════════════════════════════════════════════════
// Render target / texture management
// ═══════════════════════════════════════════════════════════════════════════

void BasicShadowMapTechnique::setRenderTarget(IRenderTarget* rt)
{
    _shadowMapRT = rt;
    if (_shadowMapRT) {
        _shadowExtent = _shadowMapRT->getExtent();
        _directionalPass.setShadowExtent(_shadowExtent);
        _pointPass.setShadowExtent(_shadowExtent);
    }
    refreshFromRenderTarget();
}

void BasicShadowMapTechnique::refreshFromRenderTarget()
{
    if (!_shadowMapRT || !_render) return;

    const auto& depthDesc = _shadowMapRT->getDepthAttachmentDesc();
    if (!depthDesc.has_value()) return;

    rebuildLayerTextures();
    _directionalPass.refreshPipeline(depthDesc->format);
    _pointPass.refreshPipeline(depthDesc->format);
}

void BasicShadowMapTechnique::rebuildLayerTextures()
{
    if (!_render || !_shadowMapRT) return;

    auto* frameBuffer  = _shadowMapRT->getCurFrameBuffer();
    auto* depthTexture = frameBuffer ? frameBuffer->getDepthTexture() : nullptr;
    if (!depthTexture) return;

    auto shadowImage = depthTexture->getImageShared();
    if (!shadowImage) return;

    // Directional: layer 0
    auto textureFactory = _render->getTextureFactory();
    auto dirView = textureFactory->createImageView(
        shadowImage,
        ImageViewCreateInfo{
            .label          = "BasicShadowMap_DirectionalDepthView",
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Depth,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        });
    if (dirView) {
        auto dirTexture = Texture::wrap(shadowImage, dirView, "BasicShadowMap_DirectionalDepthTexture");
        _directionalPass.setDepthTexture(dirTexture, dirView);
    }

    // Point faces: layers 1..36
    _pointPass.rebuildFaceTextures(shadowImage);
}

// ═══════════════════════════════════════════════════════════════════════════
// Output accessors
// ═══════════════════════════════════════════════════════════════════════════

Texture* BasicShadowMapTechnique::getDirectionalDepthTexture() const
{
    return _directionalPass.getDepthTexture();
}

Texture* BasicShadowMapTechnique::getPointFaceDepthTexture(uint32_t lightIndex, uint32_t faceIndex) const
{
    return _pointPass.getFaceDepthTexture(lightIndex, faceIndex);
}

// ═══════════════════════════════════════════════════════════════════════════
// GUI
// ═══════════════════════════════════════════════════════════════════════════

void BasicShadowMapTechnique::renderGUI()
{
    if (!ImGui::TreeNode("Basic Shadow Map Technique")) {
        return;
    }

    if (ImGui::TreeNode("Stats")) {
        ImGui::Text("Technique: Basic Shadow Map");
        ImGui::Text("Resolution: %u", _settings.resolution);
        ImGui::Text("Point lights: %u / %u", _lastPreparedPointLightCount, _settings.getEffectivePointLightCount());
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Passes")) {
        if (ImGui::TreeNode("Directional")) {
            _directionalPass.renderGUI();
            ImGui::TextDisabled("No runtime controls");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Point")) {
            _pointPass.renderGUI();
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    ImGui::TreePop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Payload / Point shadow matrix calculation
// ═══════════════════════════════════════════════════════════════════════════

BasicShadowFramePayload BasicShadowMapTechnique::buildFramePayload(uint32_t flightIndex, const RenderFrameData& frameData) const
{
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
        .pointLightCount             = pointLightCount,
        .directionalEnabled          = frameData.bHasDirectionalLight && _settings.directionalEnabled,
        .pointEnabled                = _settings.pointLightEnabled && pointLightCount > 0,
        .pointIndirectRequested      = _settings.pointLightUseIndirect,
        .pointIndirectCullEnabled    = _settings.pointLightIndirectCullEnabled,
    };
    populatePointShadowMatrices(frameData, payload.frameUBO, pointLightCount);
    return payload;
}

void BasicShadowMapTechnique::populatePointShadowMatrices(const RenderFrameData& frameData, FrameUBO& frameDataUBO, uint32_t pointLightCount) const
{
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
