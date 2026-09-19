#include "GameEditor/UI/Viewport/EditorViewportOverlayRecord.h"

#include "Core/Math/AABB.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/TransformSystem.h"
#include "Scene3D/TransformComponent.h"
#include "GameEditor/EditorLayer.h"
#include "RHI/Backend/TextureLibrary.h"
#include "Render/Resources/FontManager.h"
#include "Render2D/Render2D.h"
#include "Render3D/Common/CameraFrustumOverlay.h"
#include "Render/Adapters/Debug/PhysicsDebugDraw.h"
#include "Scene/Core/Scene.h"

#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

namespace
{

/// Selected-camera wireframe tint: the one editor accent that reads as "this is
/// the view I am looking through".
constexpr glm::vec4 kSelectedCameraFrustumColor = {1.0f, 0.85f, 0.2f, 1.0f};

void recordEntityBounds(Entity* entity, const glm::vec4& color)
{
    if (!entity || !entity->isValid()) {
        return;
    }
    Scene* scene = entity->getScene();
    if (!scene || !entity->hasComponent<TransformComponent>()) {
        return;
    }
    auto* tc = entity->getComponent<TransformComponent>();
    if (!tc) {
        return;
    }
    TransformSystem::computeWorldMatrix(tc);
    const glm::mat4 worldMatrix = tc->getWorldMatrix();
    auto& registry = scene->getRegistry();
    const auto handle = entity->getHandle();
    AABB worldBounds;
    bool hasBounds = false;
    const auto addBounds = [&](const AABB& bounds) {
        if (bounds.max.x < bounds.min.x) {
            return;
        }
        worldBounds.merge(bounds.transformed(worldMatrix));
        hasBounds = true;
    };

    if (const auto* mesh = registry.try_get<StaticMeshComponent>(handle)) {
        if (auto* resolvedMesh = mesh->getMesh()) {
            addBounds(resolvedMesh->boundingBox);
        }
    }
    if (const auto* mesh = registry.try_get<SkinnedMeshComponent>(handle)) {
        if (auto* resolvedMesh = mesh->getMesh()) {
            addBounds(resolvedMesh->boundingBox);
        }
    }
    if (!hasBounds) {
        return;
    }

    Render2D::makeWireBox(glm::translate(glm::mat4(1.0f), worldBounds.getCenter()),
                          (worldBounds.max - worldBounds.min) * 0.5f,
                          color);
}

void recordSelectedEntityBounds(const EditorLayer& layer)
{
    const auto& selections = layer.getSelections();
    if (selections.empty()) {
        return;
    }
    constexpr glm::vec4 kPrimarySelectionColor   = {0.98f, 0.69f, 0.23f, 1.0f};
    constexpr glm::vec4 kSecondarySelectionColor = {0.78f, 0.60f, 0.28f, 1.0f};
    for (size_t i = 0; i < selections.size(); ++i) {
        recordEntityBounds(selections[i], i == 0 ? kPrimarySelectionColor : kSecondarySelectionColor);
    }
}

void recordEditorWorldGrid()
{
    constexpr int   kHalf  = 20;
    constexpr float kStep  = 1.0f;
    const glm::vec4 minor{0.22f, 0.24f, 0.28f, 1.0f};
    const glm::vec4 axisX{0.62f, 0.24f, 0.24f, 1.0f};
    const glm::vec4 axisZ{0.24f, 0.38f, 0.72f, 1.0f};
    const float     extent = static_cast<float>(kHalf) * kStep;
    for (int i = -kHalf; i <= kHalf; ++i) {
        const float t = static_cast<float>(i) * kStep;
        Render2D::makeWorldLine({-extent, 0.0f, t}, {extent, 0.0f, t}, i == 0 ? axisX : minor);
        Render2D::makeWorldLine({t, 0.0f, -extent}, {t, 0.0f, extent}, i == 0 ? axisZ : minor);
    }
}

void recordCameraHud(EditorLayer& layer)
{
    const auto texts = layer.buildViewportCameraOverlayTexts();
    if (texts.empty()) {
        return;
    }
    Render2D::makeSprite(glm::vec3(6.0f, 6.0f, 0.0f),
                         glm::vec2(240.0f, 46.0f),
                         TextureLibrary::get().getWhiteTexture().get(),
                         glm::vec4(0.0f, 0.0f, 0.0f, 0.36f));
    for (const auto& text : texts) {
        auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, text.fontSize);
        if (!font) {
            continue;
        }
        Render2D::makeText(text.text, glm::vec3(text.viewportPos, text.depth), text.color, font.get());
    }
}

/// Compact FOV wireframe for the camera the user selected: the same camera the
/// preview inset shows. The camera body is a world-space mesh
/// (CameraMeshLinkageRule); these lines stay procedural so they follow FOV
/// without a new pipeline, and they belong to the editor's overlay pass because
/// they report the editor's own selection.
void recordSelectedCameraFrustum(EditorLayer& layer)
{
    Entity* selected = layer.getCameraPreviewEntity();
    if (!selected) {
        return;
    }
    auto* camera = selected->getComponent<CameraComponent>();
    if (!camera) {
        return;
    }

    std::vector<RenderOverlayLine3D> lines;
    appendCameraFrustumOverlayLines(lines,
                                    camera->getFreeView(),
                                    camera->getProjection(),
                                    kSelectedCameraFrustumColor);
    for (const RenderOverlayLine3D& line : lines) {
        Render2D::makeWorldLine(line.from, line.to, line.color);
    }
}

void recordPhysicsCollision(EditorLayer& layer)
{
    Scene* scene = layer.getViewportInteractionScene();
    if (!scene) {
        return;
    }
    drawPhysicsCollisionDebug(
        *scene,
        PhysicsDebugLineCollector{
            .sphere = [](const glm::vec3& center, float radius, const glm::vec4& color) {
                Render2D::makeWireSphere(center, radius, color);
            },
            .box = [](const glm::mat4& model, const glm::vec3& halfExtent, const glm::vec4& color) {
                Render2D::makeWireBox(model, halfExtent, color);
            },
        });
}

} // namespace

void recordEditorWorldViewportOverlays(EditorLayer& layer, bool bDepthTestedWorld)
{
    recordEditorWorldGrid();
    layer.gizmo().recordOverlay();
    recordCameraHud(layer);
    recordSelectedCameraFrustum(layer);
    if (!bDepthTestedWorld) {
        return;
    }
    recordPhysicsCollision(layer);
    recordSelectedEntityBounds(layer);
}

void recordEditorCanvasSelectionOverlay(const Rect2D& rect, const glm::vec2& uiScale, const glm::vec2& offset)
{
    // Outline + resize handles in target pixels. The widget rect uses the
    // same uiScale/offset as the preview snapshot so pan/zoom stay coherent.
    auto* white = TextureLibrary::get().getWhiteTexture().get();
    if (!white) {
        return;
    }
    const glm::vec2 pos  = offset + rect.pos * uiScale;
    const glm::vec2 size = rect.extent * uiScale;
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return;
    }
    const glm::vec4 color(0.25f, 0.62f, 1.0f, 1.0f);
    const float     thickness = 2.0f;
    Render2D::makeSprite(glm::vec3(pos.x, pos.y, 0.0f), glm::vec2(size.x, thickness), white, color);
    Render2D::makeSprite(glm::vec3(pos.x, pos.y + size.y - thickness, 0.0f), glm::vec2(size.x, thickness), white, color);
    Render2D::makeSprite(glm::vec3(pos.x, pos.y, 0.0f), glm::vec2(thickness, size.y), white, color);
    Render2D::makeSprite(glm::vec3(pos.x + size.x - thickness, pos.y, 0.0f), glm::vec2(thickness, size.y), white, color);
    const float handleSize = 7.0f;
    const auto  drawHandle = [&](const glm::vec2& center) {
        Render2D::makeSprite(glm::vec3(center.x - handleSize * 0.5f, center.y - handleSize * 0.5f, 0.0f),
                             glm::vec2(handleSize, handleSize),
                             white,
                             color);
    };
    drawHandle({pos.x, pos.y});
    drawHandle({pos.x + size.x, pos.y});
    drawHandle({pos.x, pos.y + size.y});
    drawHandle({pos.x + size.x, pos.y + size.y});
    drawHandle({pos.x + size.x * 0.5f, pos.y});
    drawHandle({pos.x + size.x * 0.5f, pos.y + size.y});
    drawHandle({pos.x, pos.y + size.y * 0.5f});
    drawHandle({pos.x + size.x, pos.y + size.y * 0.5f});
}

} // namespace ya
