#include "GameEditor/EditorLayerInternal.h"
#include "GameEditor/UI/Ops/EditorHierarchyOps.h"

#include "Core/Os/Os.h"
#include "ECS/System/RayCastMousePickingSystem.h"
#include "ECS/Systems/TransformSystem.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Render.h"

#include <cmath>

namespace ya
{

namespace
{
/// Pixel-accurate pick: read the entity id written by the viewport graph's
/// entity-id pass at the cursor position, then map the id back to an entity.
Entity* pickEntityFromEntityIdImage(IRender*                             render,
                                    const std::shared_ptr<RenderTexture>& idImage,
                                    Scene*                              scene,
                                    float                               viewportX,
                                    float                               viewportY,
                                    float                               viewportWidth,
                                    float                               viewportHeight)
{
    if (!render || !idImage || !idImage->getImage() || !scene) {
        return nullptr;
    }

    const Extent2D extent = idImage->getExtent();
    if (extent.width == 0 || extent.height == 0) {
        return nullptr;
    }

    // The id target may be rendered at a different resolution than the ImGui
    // viewport panel (frame buffer scale); map the cursor through the ratio.
    const float scaleX = static_cast<float>(extent.width) / std::max(viewportWidth, 1.0f);
    const float scaleY = static_cast<float>(extent.height) / std::max(viewportHeight, 1.0f);
    const int32_t pixelX = std::clamp(static_cast<int32_t>(viewportX * scaleX), 0, static_cast<int32_t>(extent.width - 1));
    const int32_t pixelY = std::clamp(static_cast<int32_t>(viewportY * scaleY), 0, static_cast<int32_t>(extent.height - 1));

    auto readback = render->getResourceFactory()->createBuffer(BufferCreateInfo{
        .label       = "EditorEntityIdPickReadback",
        .usage       = EBufferUsage::TransferDst,
        .size        = sizeof(uint32_t),
        .memoryUsage = EMemoryUsage::GpuToCpu,
    });
    if (!readback) {
        return nullptr;
    }

    auto* cmdBuf = render->beginIsolateCommands("EditorEntityPick");
    cmdBuf->transitionImageLayoutAuto(idImage->getImage(), EImageLayout::TransferSrc);
    cmdBuf->copyImageToBuffer(
        idImage->getImage(),
        EImageLayout::TransferSrc,
        readback.get(),
        {BufferImageCopy{
            .bufferOffset      = 0,
            .bufferRowLength   = 0,
            .bufferImageHeight = 0,
            .imageSubresource  = {
                .aspectMask     = EImageAspect::Color,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
            .imageOffsetX      = pixelX,
            .imageOffsetY      = pixelY,
            .imageOffsetZ      = 0,
            .imageExtentWidth  = 1,
            .imageExtentHeight = 1,
            .imageExtentDepth  = 1,
        }});
    render->endIsolateCommands(cmdBuf);

    const uint32_t* mapped = readback->map<uint32_t>();
    if (!mapped) {
        return nullptr;
    }
    const uint32_t entityId = *mapped;
    if (entityId == 0) {
        return nullptr;
    }
    return scene->getEntityByEnttID(entt::entity{entityId});
}

} // namespace

void EditorLayer::onEvent(const Event& event)
{
    if (_app && !_app->isStopped()) {
        return;
    }

    // Handle viewport-specific events when focused
    // Example: Camera controls, object picking, gizmo manipulation

    // Track right mouse drag for camera rotation (to prevent context menu popup)
    switch (event.getEventType()) {
    case EEvent::MouseButtonPressed:
    {
        auto& mouseEvent = static_cast<const MouseButtonPressedEvent&>(event);
        if (mouseEvent.GetMouseButton() == EMouse::Right && bViewportHovered) {
            _rightMousePressPos  = _app->getLastMousePos();
            _bRightMouseDragging = false; // Not dragging yet, just pressed
        }
    } break;
    case EEvent::MouseMoved:
    {
        // If right mouse is held and we moved significantly, mark as dragging
        if (_app && _app->getInputManager().isMouseButtonDown(EMouse::Right) && bViewportHovered) {
            glm::vec2 currentPos = _app->getLastMousePos();
            float     dist       = glm::length(currentPos - _rightMousePressPos);
            if (dist > 3.0f) { // Threshold to distinguish click from drag
                _bRightMouseDragging = true;
            }
        }
    } break;
    case EEvent::MouseButtonReleased:
    {
        auto& mouseEvent = static_cast<const MouseButtonReleasedEvent&>(event);
        if (mouseEvent.GetMouseButton() == EMouse::Right) {
            _bRightMouseDragging = false;
        }
    } break;
    default:
        break;
    }

    if (!bViewportFocused) {
        return; // Only process other events when viewport is focused
    }

    // Example event handling (extend as needed):
    switch (event.getEventType()) {
    case EEvent::MouseMoved:
    {
    } break;

    case EEvent::MouseButtonPressed:
        break;
    case EEvent::MouseButtonReleased:
    {
        // Handle viewport clicks (object selection, gizmo interaction)
        auto& mouseEvent = static_cast<const MouseButtonReleasedEvent&>(event);
        // Only pick on left click and when gizmo is not being used
        if (mouseEvent.GetMouseButton() == EMouse::Left) {
            if (_gizmo.consumeReleasePick()) {
                break;
            }
            if (!_gizmo.isActive()) {
                float localX{}, localY{};
                auto  cursorPos = _app->getLastMousePos();
                if (screenToViewport(cursorPos.x, cursorPos.y, localX, localY)) {
                    pickEntity(localX, localY);
                }
            }
        }
    } break;

    case EEvent::MouseScrolled:
    {
        // Handle camera zoom in viewport
    } break;

    case EEvent::KeyPressed:
    {
        auto& keyEvent = static_cast<const KeyPressedEvent&>(event);

        if (keyEvent.getKeyCode() == EKey::K_F) {
            // Focus camera on the whole selection (merged bounds)
            if (!getSelections().empty()) {
                focusCameraOnSelection();
            }
        }

        if (canViewportAuthor()) {
            if (keyEvent.getKeyCode() == EKey::Delete) {
                cmdDeleteSelection();
            }
            else if (keyEvent.getKeyCode() == EKey::K_D &&
                     (keyEvent.isCtrlPressed() || keyEvent.isMetaPressed())) {
                cmdDuplicateSelection();
            }
        }

    } break;

    default:
        break;
    }
}

void EditorLayer::pickEntity(float viewportLocalX, float viewportLocalY)
{
    YA_PROFILE_FUNCTION_LOG();
    auto* app = App::get();
    if (!app) {
        return;
    }

    auto* scene = getViewportInteractionScene();
    if (!scene) {
        return;
    }

    // The camera of the View the host window shows, from the app's arrangement
    // for the last recorded frame. Picking runs on input, i.e. before this
    // tick's render, so it reads the arrangement rather than a live declaration.
    const auto& displayedView = app->getRenderServices().getDisplayedView();
    glm::mat4   view         = displayedView.view;
    glm::mat4   projection   = displayedView.projection;

    // Pixel-accurate picking: read the entity id the viewport graph wrote at
    // the cursor position. Falls back to the CPU raycast when the id target is
    // unavailable (e.g. before the first rendered frame) or misses.
    Entity* pickedEntity = pickEntityFromEntityIdImage(
        app->getRenderServices().getRender(),
        getEntityIdPickImage(),
        scene,
        viewportLocalX,
        viewportLocalY,
        _viewportSize.x,
        _viewportSize.y);
    if (!pickedEntity) {
        pickedEntity = RayCastMousePickingSystem::pickEntity(
            scene,
            viewportLocalX,
            viewportLocalY,
            _viewportSize.x,
            _viewportSize.y,
            view,
            projection);
    }

    // Both pick paths report the entity that owns the drawn mesh, which for a
    // model instance is one managed child per mesh. Moving that child only moves
    // that mesh, so a plain click selects the instance root instead; Alt+click
    // keeps the pixel-accurate leaf for editing an individual mesh's material.
    Entity* selectionTarget = pickedEntity;
    if (selectionTarget && (Os::queryKeyModState() & EKeyMod::Alt) == 0) {
        selectionTarget = editorResolveInstanceRoot(*scene, selectionTarget);
    }

    // Update selection
    if (selectionTarget) {
        // Ctrl/Cmd toggles membership, Shift extends from the anchor; plain
        // clicks replace the selection.
        _selection.handleEntityClick(selectionTarget);
        if (selectionTarget != pickedEntity) {
            YA_CORE_INFO("Picked entity: {} (model instance root; Alt+click for '{}')",
                         selectionTarget->getName(),
                         pickedEntity->getName());
        }
        else {
            YA_CORE_INFO("Picked entity: {}", selectionTarget->getName());
        }
    }
    else {
        _selection.setSelection(nullptr);
        YA_CORE_INFO("No entity picked");
    }
}

void EditorLayer::focusCameraOnSelection()
{
    const auto& selections = getSelections();
    if (selections.empty()) {
        return;
    }

    // Primary selection drives the fallback pivot; bounds are merged across
    // every valid selected entity so multi-select frames the whole group.
    Entity* primary = selections.front();
    if (!primary || !primary->isValid()) {
        return;
    }
    auto* app = App::get();
    if (!app) {
        return;
    }

    if (!primary->hasComponent<TransformComponent>()) {
        return;
    }
    auto* primaryTc = primary->getComponent<TransformComponent>();
    // Focus the world position, not the local one: hierarchical children would
    // otherwise pull the camera to their local origin. No-op when already clean.
    TransformSystem::computeWorldMatrix(primaryTc);
    const glm::mat4 primaryWorldMatrix = primaryTc->getWorldMatrix();

    glm::vec3 entityPos      = glm::vec3(primaryWorldMatrix[3]);
    float     boundingRadius = 0.0f;
    AABB      mergedBounds;
    bool      bHasBounds = false;

    for (Entity* entity : selections) {
        if (!entity || !entity->isValid() || !entity->hasComponent<TransformComponent>()) {
            continue;
        }
        auto* tc = entity->getComponent<TransformComponent>();
        TransformSystem::computeWorldMatrix(tc);
        const glm::mat4 worldMatrix = tc->getWorldMatrix();

        if (auto* scene = entity->getScene()) {
            const auto& registry = scene->getRegistry();
            const auto  handle   = entity->getHandle();
            const auto  addBounds = [&](const AABB& bounds)
            {
                if (bounds.max.x < bounds.min.x) {
                    return;
                }
                mergedBounds.merge(bounds.transformed(worldMatrix));
                bHasBounds = true;
            };
            if (const auto* mesh = registry.try_get<StaticMeshComponent>(handle)) {
                if (auto* m = mesh->getMesh()) {
                    addBounds(m->boundingBox);
                }
            }
            if (const auto* mesh = registry.try_get<SkinnedMeshComponent>(handle)) {
                if (auto* m = mesh->getMesh()) {
                    addBounds(m->boundingBox);
                }
            }
        }
    }

    if (bHasBounds) {
        entityPos      = mergedBounds.getCenter();
        boundingRadius = glm::length(mergedBounds.max - mergedBounds.min) * 0.5f;
    }

    const float distance = boundingRadius > 0.001f
                               ? boundingRadius / std::tan(glm::radians(_camera.getFov() * 0.5f)) * 1.2f
                               : 10.0f; // Fallback when the selection has no mesh bounds.
    const glm::vec3 camPos = _camera.getPosition();

    glm::vec3 camToEntity = entityPos - camPos;
    if (glm::dot(camToEntity, camToEntity) < 1e-6f) {
        // Camera sits exactly on the entity: keep the current view direction.
        camToEntity = -glm::vec3(_camera.getViewMatrix()[2]);
    }
    camToEntity = glm::normalize(camToEntity);

    // Position camera behind entity at fixed distance
    const glm::vec3 newCamPos = entityPos - camToEntity * distance;

    glm::vec3 newCamRotation{};
    {
        glm::vec3 newCamToEntity = glm::normalize(entityPos - newCamPos);
        // y 为 dir 与 xoz 平面的夹角的正弦值 sin(theta), arcsin(sin(theta)) 得到角度值 theta, 即 pitch
        float pitch = glm::degrees(std::asin(newCamToEntity.y));


        glm::vec2 xozPlane = glm::vec2{FMath::Vector::WorldRight.x, FMath::Vector::WorldForward.z};
        (void)xozPlane;
        //  现在我们在 xoz 平面上, 想象一个平面坐标系, dir.x 向右， dir.z 向上(1),  yaw 即为角度值
        // tan(theta) = y/x, 即通过角度求斜率, atan(y/x) 会丢失象限信息(如45度与135度), 即单独的 dir.x 和 dir.z 无法确定正确的角度
        // 使用 atan2(y,x) 可以保留象限信息, 通过 dir.x 和 dir.z 的正负号确定正确的角度
        // atan2(1,1) = 45度, atan2(1,-1)=135度, atan2(-1,-1)=-135度, atan2(-1,1)=-45度

        // 注意这里 direction.z 应该取反，因为在右手坐标系中，Z 轴正方向是向屏幕外侧的，
        // 这样 xoz 就与 屏幕坐标系的 y 向上相反，所以整个方向旋转 180 度(单独z相反会导致左右相反)
        if constexpr (FMath::Vector::IsRightHanded) {
            newCamToEntity.z = -newCamToEntity.z;
            newCamToEntity.x = -newCamToEntity.x; // 否则会左右相反
        }
        float yaw = glm::degrees(std::atan2(newCamToEntity.x, newCamToEntity.z));

        newCamRotation = glm::vec3(pitch, yaw, 0.0f);
    }

    _camera.setPosition(newCamPos);
    _camera.setRotation(newCamRotation);
}
} // namespace ya
