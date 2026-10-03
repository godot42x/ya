#include "GameEditor/UI/Viewport/EditorViewportOverlayRecord.h"

#include "Core/Math/AABB.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/TransformSystem.h"
#include "Scene3D/TransformComponent.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/EditorViewProducer.h"
#include "GameRuntime/Render/SceneCameraQuery.h"
#include "RHI/Backend/TextureLibrary.h"
#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/TextRaster.h"
#include "Render2D/ScreenDrawList.h"
#include "Render3D/WorldDraw.h"
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

void recordEntityBounds(WorldDrawList& list, Entity* entity, const glm::vec4& color)
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

    list.makeWireBox(glm::translate(glm::mat4(1.0f), worldBounds.getCenter()),
                          (worldBounds.max - worldBounds.min) * 0.5f,
                          color);
}

void recordSelectedEntityBounds(WorldDrawList& list, const EditorLayer& layer)
{
    const auto& selections = layer.getSelections();
    if (selections.empty()) {
        return;
    }
    constexpr glm::vec4 kPrimarySelectionColor   = {0.98f, 0.69f, 0.23f, 1.0f};
    constexpr glm::vec4 kSecondarySelectionColor = {0.78f, 0.60f, 0.28f, 1.0f};
    for (size_t i = 0; i < selections.size(); ++i) {
        recordEntityBounds(list, selections[i], i == 0 ? kPrimarySelectionColor : kSecondarySelectionColor);
    }
}

void recordCameraHud(ScreenDrawList& list, EditorLayer& layer)
{
    const auto texts = layer.buildViewportCameraOverlayTexts();
    if (texts.empty()) {
        return;
    }
    const float density = layer.viewportPixelDensity() > 0.0f ? layer.viewportPixelDensity() : 1.0f;
    list.makeSprite(glm::vec3(6.0f, 6.0f, 0.0f) * density,
                         glm::vec2(240.0f, 46.0f) * density,
                         TextureLibrary::get().getWhiteTexture().get(),
                         glm::vec4(0.0f, 0.0f, 0.0f, 0.36f));
    for (const auto& text : texts) {
        const FTextRasterPlan plan = planTextRaster(static_cast<float>(text.fontSize),
                                                    glm::vec2(1.0f),
                                                    density,
                                                    glm::vec2(1.0f));
        auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, plan.rasterPx);
        if (!font) {
            continue;
        }
        list.makeText(text.text,
                      glm::vec3(text.viewPos * density, text.depth),
                      text.color,
                      font.get(),
                      plan.residual);
    }
}

/// Compact FOV wireframe for the camera the user selected: the same camera the
/// preview inset shows. The camera body is a world-space mesh
/// (CameraMeshLinkageRule); these lines stay procedural so they follow FOV
/// without a new pipeline, and they belong to the editor's overlay pass because
/// they report the editor's own selection.
void recordSelectedCameraFrustum(WorldDrawList& list, EditorLayer& layer)
{
    Entity* selected = layer.getCameraPreviewEntity();
    if (!selected) {
        return;
    }
    auto* camera = selected->getComponent<CameraComponent>();
    if (!camera) {
        return;
    }

    glm::mat4 view       = cameraView(*selected);
    glm::mat4 projection = camera->getProjection(camera->_aspectRatio);
    // The wireframe is the frustum the preview inset shows. Pixel-perfect size
    // depends on that inset's pixel height; a fixed aspect would not match it.
    if (camera->_pixelPerfect) {
        const Rect2D preview = EditorViewProducer::previewRect(layer.getViewportRect());
        if (preview.extent.x > 0.0f && preview.extent.y >= 1.0f) {
            const CameraRenderMatrices frame = buildCameraRenderMatrices(*camera, selected, preview.extent);
            view                             = frame.view;
            projection                       = frame.projection;
        }
    }
    std::vector<RenderOverlayLine3D> lines;
    appendCameraFrustumOverlayLines(lines, view, projection, kSelectedCameraFrustumColor);
    for (const RenderOverlayLine3D& line : lines) {
        list.makeLine(line.from, line.to, line.color);
    }
}

void recordPhysicsCollision(WorldDrawList& list, EditorLayer& layer)
{
    Scene* scene = layer.getViewportInteractionScene();
    if (!scene) {
        return;
    }
    drawPhysicsCollisionDebug(
        *scene,
        PhysicsDebugLineCollector{
            .sphere = [&list](const glm::vec3& center, float radius, const glm::vec4& color) {
                list.makeWireSphere(center, radius, color);
            },
            .box = [&list](const glm::mat4& model, const glm::vec3& halfExtent, const glm::vec4& color) {
                list.makeWireBox(model, halfExtent, color);
            },
        });
}

} // namespace

void recordEditorViewportScreenOverlays(ScreenDrawList& list, EditorLayer& layer)
{
    layer.gizmo().recordOverlay(list);
    recordCameraHud(list, layer);
}

void recordEditorViewportWorldOverlays(WorldDrawList& list, EditorLayer& layer, bool bDepthTestedWorld)
{
    recordSelectedCameraFrustum(list, layer);
    if (!bDepthTestedWorld) {
        return;
    }
    recordPhysicsCollision(list, layer);
    recordSelectedEntityBounds(list, layer);
    if (layer.tileBrush().isStroking() || layer.tileBrush().isEngaged()) {
        layer.tileBrush().recordWorldOverlay(list);
    }
}

void recordEditorCanvasDesignFrame(ScreenDrawList& list, const glm::vec2& designSize, const glm::vec2& uiScale, const glm::vec2& offset, float gridStep)
{
    auto* white = TextureLibrary::get().getWhiteTexture().get();
    const glm::vec2 size = designSize * uiScale;
    if (!white || size.x <= 0.0f || size.y <= 0.0f) {
        return;
    }
    const glm::vec4 color(0.62f, 0.62f, 0.66f, 0.9f);
    const float     thickness = 1.0f;
    list.makeSprite(glm::vec3(offset.x, offset.y, 0.0f), glm::vec2(size.x, thickness), white, color);
    list.makeSprite(glm::vec3(offset.x, offset.y + size.y - thickness, 0.0f), glm::vec2(size.x, thickness), white, color);
    list.makeSprite(glm::vec3(offset.x, offset.y, 0.0f), glm::vec2(thickness, size.y), white, color);
    list.makeSprite(glm::vec3(offset.x + size.x - thickness, offset.y, 0.0f), glm::vec2(thickness, size.y), white, color);

    if (gridStep > 0.0f) {
        const glm::vec2 cell = gridStep * uiScale;
        if (cell.x >= 6.0f && cell.y >= 6.0f) {
            const glm::vec4 grid(0.5f, 0.5f, 0.55f, 0.22f);
            for (float x = gridStep; x < designSize.x - 0.5f; x += gridStep) {
                list.makeSprite(glm::vec3(offset.x + x * uiScale.x, offset.y, 0.0f),
                                glm::vec2(thickness, size.y), white, grid);
            }
            for (float y = gridStep; y < designSize.y - 0.5f; y += gridStep) {
                list.makeSprite(glm::vec3(offset.x, offset.y + y * uiScale.y, 0.0f),
                                glm::vec2(size.x, thickness), white, grid);
            }
        }
    }
}

void recordEditorCanvasSelectionOverlay(ScreenDrawList& list, const Rect2D& rect, const glm::vec2& uiScale, const glm::vec2& offset)
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
    list.makeSprite(glm::vec3(pos.x, pos.y, 0.0f), glm::vec2(size.x, thickness), white, color);
    list.makeSprite(glm::vec3(pos.x, pos.y + size.y - thickness, 0.0f), glm::vec2(size.x, thickness), white, color);
    list.makeSprite(glm::vec3(pos.x, pos.y, 0.0f), glm::vec2(thickness, size.y), white, color);
    list.makeSprite(glm::vec3(pos.x + size.x - thickness, pos.y, 0.0f), glm::vec2(thickness, size.y), white, color);
    const float handleSize = 7.0f;
    const auto  drawHandle = [&](const glm::vec2& center) {
        list.makeSprite(glm::vec3(center.x - handleSize * 0.5f, center.y - handleSize * 0.5f, 0.0f),
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
