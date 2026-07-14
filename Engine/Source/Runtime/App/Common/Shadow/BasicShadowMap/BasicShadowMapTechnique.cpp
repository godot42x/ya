#include "BasicShadowMapTechnique.h"

#include "Core/Profiling/Instrumentor.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/RenderResourceFactory.h"
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
}

void BasicShadowMapTechnique::destroy()
{
    _directionalPass.destroy();
    _pointPass.destroy();
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

std::optional<RGPassHandle> BasicShadowMapTechnique::appendGraphPasses(
    RenderGraph& graph,
    uint32_t flightIndex,
    const RenderFrameData& frameData)
{
    if (!_settings.isEnabled()) return std::nullopt;

    auto payload = buildFramePayload(flightIndex, frameData);
    payload.pointLightCount = std::min(_lastPreparedPointLightCount, static_cast<uint32_t>(MAX_POINT_LIGHTS));

    std::optional<RGPassHandle> lastPass;
    if (payload.directionalEnabled()) {
        lastPass = _directionalPass.appendGraphPass(graph, payload, lastPass);
    }
    if (payload.pointEnabled()) {
        lastPass = _pointPass.appendGraphPasses(graph, payload, lastPass);
    }
    return lastPass;
}

// ═══════════════════════════════════════════════════════════════════════════
// Render target / texture management
// ═══════════════════════════════════════════════════════════════════════════

void BasicShadowMapTechnique::refreshShadowResources(const std::shared_ptr<IImage>& depthImage, EFormat::T depthFormat, Extent2D shadowExtent)
{
    if (!_render || !depthImage) return;

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

    // Directional: layer 0
    auto* resourceFactory = _render->getResourceFactory();
    auto  dirView = resourceFactory->createImageView(
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
        _directionalPass.setDepthAttachment(shadowImage, dirView);
    }

    // Point faces: layers 1..36
    _pointPass.rebuildFaceTextures(shadowImage);
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
        ImGui::Text("Point Indirect: %s", _settings.pointLightUseIndirect ? "On" : "Off");
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
