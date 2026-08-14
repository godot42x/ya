#include "Editor/EditorLayerInternal.h"

#include "ECS/System/RayCastMousePickingSystem.h"
#include "Gameplay/Systems/TransformSystem.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderImage.h"
#include "RHI/Render.h"

#include <cmath>
#include <functional>

namespace ya
{

namespace
{

/// Pixel-accurate pick: read the entity id written by the viewport graph's
/// entity-id pass at the cursor position, then map the id back to an entity.
Entity* pickEntityFromEntityIdImage(IRender*                             render,
                                    const std::shared_ptr<RenderImage>& idImage,
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
    if (_app && !_app->isStopped() && !isViewportMode2D()) {
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
        else if (isViewportMode2D() && mouseEvent.GetMouseButton() == EMouse::Left && bViewportHovered) {
            // 2D canvas: select on press, resize handles take priority, then
            // hit the preview tree (select + start move), empty clears.
            beginCanvasPress();
        }
    } break;
    case EEvent::MouseMoved:
    {
        // If right mouse is held and we moved significantly, mark as dragging
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && bViewportHovered) {
            glm::vec2 currentPos = _app->getLastMousePos();
            float     dist       = glm::length(currentPos - _rightMousePressPos);
            if (dist > 3.0f) { // Threshold to distinguish click from drag
                _bRightMouseDragging = true;
            }
        }

        // 2D canvas panning (right or middle drag).
        if (isViewportMode2D() && bViewportHovered) {
            const bool bPanning = ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                                  ImGui::IsMouseDown(ImGuiMouseButton_Middle);
            if (bPanning) {
                const glm::vec2 currentPos = _app->getLastMousePos();
                if (_bCanvasPanning) {
                    _canvasPan += currentPos - _canvasPanLastMouse;
                }
                _bCanvasPanning     = true;
                _canvasPanLastMouse = currentPos;
            }
            else {
                _bCanvasPanning = false;
            }
        }

        // 2D canvas widget manipulation: left-drag on the designer preview
        // (move or resize). Never fights the right/middle pan.
        if (isViewportMode2D() && bViewportHovered && _canvasPressHit &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            updateCanvasDrag();
        }
    } break;
    case EEvent::MouseButtonReleased:
    {
        auto& mouseEvent = static_cast<const MouseButtonReleasedEvent&>(event);
        if (mouseEvent.GetMouseButton() == EMouse::Right) {
            // Reset drag state on release (after a short delay to let ImGui process)
            // We keep the flag true briefly so context menu check can see it
            facade().timerManager.delayCall(50, [this]() {
                _bRightMouseDragging = false;
            });
        }
    } break;
    default:
        break;
    }

    if (!bViewportFocused) {
        return; // Only process other events when viewport is focused
    }

    // 2D/3D viewport mode shortcuts work in both modes.
    if (event.getEventType() == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (keyEvent._keyCode == EKey::K_2) {
            setViewportMode(EViewportMode::Mode2D);
            return;
        }
        if (keyEvent._keyCode == EKey::K_3) {
            setViewportMode(EViewportMode::Mode3D);
            return;
        }
    }

    if (isViewportMode2D()) {
        switch (event.getEventType()) {
        case EEvent::MouseButtonReleased:
        {
            auto& mouseEvent = static_cast<const MouseButtonReleasedEvent&>(event);
            if (mouseEvent.GetMouseButton() == EMouse::Left) {
                if (_bCanvasPressActive) {
                    // The press already selected and started the drag session;
                    // release only ends it (no double pick).
                    endCanvasPress();
                }
                else if (!_bCanvasPanning) {
                    // Fallback for presses that began outside the viewport
                    // (hover state was false, so beginCanvasPress never ran).
                    float localX{}, localY{};
                    auto  cursorPos = _app->getLastMousePos();
                    if (screenToViewport(cursorPos.x, cursorPos.y, localX, localY)) {
                        pickNode2D(localX, localY);
                    }
                }
            }
        } break;
        case EEvent::MouseScrolled:
        {
            auto& scrollEvent = static_cast<const MouseScrolledEvent&>(event);
            const float zoomFactor = std::exp(scrollEvent.getOffsetY() * 0.12f);
            // Zoom around the viewport center so the canvas point under the
            // cursor stays fixed.
            const glm::vec2 center(_viewportSize.x * 0.5f, _viewportSize.y * 0.5f);
            const glm::vec2 newPan = center - (center - _canvasPan) * zoomFactor;
            setCanvasZoom(_canvasZoom * zoomFactor);
            _canvasPan = newPan;
        } break;
        case EEvent::KeyPressed:
        {
            auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
            if (keyEvent.getKeyCode() == EKey::Delete) {
                // Delete the selected preview widget (root is protected).
                endCanvasPress();
                _uiDesignerPanel.deleteWidget(_uiDesignerPanel.getSelectedWidget());
            }
        } break;
        default:
            break;
        }
        return;
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
            if (!isGizmoActive()) {
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
        if (_selections.size() > 0 && _selections[0]->isValid())
        {
            // Handle viewport shortcuts (W/E/R for gizmo, Delete for selection, etc.)
            switch (keyEvent._keyCode) {
            case EKey::K_W:
                _gizmoOperation = ImGuizmo::TRANSLATE;
                break;
            case EKey::K_E:
                _gizmoOperation = ImGuizmo::ROTATE;
                break;
            case EKey::K_R:
                _gizmoOperation = ImGuizmo::SCALE;
                break;
            default:
                break;
            }
        }

        if (keyEvent.getKeyCode() == EKey::K_F) {
            // Focus camera on the whole selection (merged bounds)
            if (!getSelections().empty()) {
                focusCameraOnSelection();
            }
        }


    } break;

    default:
        break;
    }
}

bool EditorLayer::isGizmoActive() const
{
    bool bUsing = ImGuizmo::IsUsing();
    bool bOver  = ImGuizmo::IsOver();
    return bUsing || bOver;
}

void EditorLayer::renderGizmo()
{
    YA_PROFILE_FUNCTION();
    // Primary selection drives the gizmo; other selected entities follow the
    // same world delta (see the manipulation block below).
    Entity* selectedEntity = getSelectedEntity();

    // CRITICAL: Do NOT call selectedEntity->isValid() before null check!
    // The entity pointer may point to destroyed memory after scene switch.
    // The scene switch handler should have cleared selection, but double-check here.
    if (!selectedEntity) {
        ImGuizmo::Enable(false);
        return; // No entity selected
    }

    // Now safe to call member functions - verify entity is still valid
    if (!selectedEntity->isValid()) {
        YA_CORE_WARN("Selected entity is invalid after scene switch, clearing selection");
        _sceneHierarchyPanel.setSelection(nullptr);
        ImGuizmo::Enable(false);
        return;
    }

    ImGuizmo::Enable(true);

    // Get transform component
    if (!selectedEntity->hasComponent<TransformComponent>()) {
        return;
    }
    auto* tc = selectedEntity->getComponent<TransformComponent>();
    if (!tc) {
        return; // No transform component
    }

    // Get camera view and projection matrices
    auto* app = App::get();
    if (!app) {
        return;
    }

    const auto& frameState = app->getRenderServices().getRenderFrameState();
    glm::mat4   view       = frameState.view;
    glm::mat4   proj       = frameState.projection;

    // Setup ImGuizmo context
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();

    // Set gizmo rect to match viewport bounds
    ImGuizmo::SetRect(_viewportBounds[0].x,
                      _viewportBounds[0].y,
                      _viewportSize.x,
                      _viewportSize.y);
    // Get parent world matrix for hierarchy transform calculations

    // Use WORLD matrix for gizmo display (so gizmo appears at actual world position)
    glm::mat4 worldTransform          = tc->getTransform();
    const glm::mat4 originalWorldTransform = worldTransform;

    // Snap settings (can be toggled with Ctrl key)
    float snap[3] = {0.0f, 0.0f, 0.0f};     // No snap by default
    bool  useSnap = ImGui::GetIO().KeyCtrl; // Hold Ctrl to enable snap

    if (useSnap) {
        // Snap values for different operations
        switch (_gizmoOperation) {
        case ImGuizmo::TRANSLATE:
            snap[0] = snap[1] = snap[2] = 0.5f; // 0.5 unit snap for translation
            break;
        case ImGuizmo::ROTATE:
            snap[0] = snap[1] = snap[2] = 15.0f; // 15 degree snap for rotation
            break;
        case ImGuizmo::SCALE:
            snap[0] = snap[1] = snap[2] = 0.1f; // 0.1 snap for scale
            break;
        default:
            break;
        }
    }

    // Manipulate transform with gizmo (using world matrix)
    if (ImGuizmo::Manipulate(
            glm::value_ptr(view),
            glm::value_ptr(proj),
            _gizmoOperation,
            _gizmoMode,
            glm::value_ptr(worldTransform),
            nullptr,
            useSnap ? snap : nullptr))
    {

        // Gizmo was used - worldTransform now contains the NEW world matrix after manipulation
        // Use TransformSystem to update transform (ensures proper computation and propagation)
        TransformSystem::setWorldTransform(tc, worldTransform);

        // Apply the same world-space delta to every other selected entity so
        // translation moves them together and rotation/scale pivot on the
        // primary entity.
        const auto& selections = getSelections();
        if (selections.size() > 1) {
            const glm::mat4 delta = worldTransform * glm::inverse(originalWorldTransform);
            for (Entity* other : selections) {
                if (!other || other == selectedEntity || !other->isValid()) {
                    continue;
                }
                if (!other->hasComponent<TransformComponent>()) {
                    continue;
                }
                auto* otherTc = other->getComponent<TransformComponent>();
                const glm::mat4 otherWorld = delta * otherTc->getTransform();
                TransformSystem::setWorldTransform(otherTc, otherWorld);
            }
        }

        // Decompose LOCAL matrix back to position/rotation/scale
        // glm::vec3 position, rotation, scale;
        // ImGuizmo::DecomposeMatrixToComponents(
        //     glm::value_ptr(newLocalMatrix),
        //     glm::value_ptr(position),
        //     glm::value_ptr(rotation),
        //     glm::value_ptr(scale));

        // // Update transform component with LOCAL values
        // // Use setters to properly propagate dirty flags to children
        // tc->_position   = position;
        // tc->_rotation   = rotation;
        // tc->_scale      = scale;
        // tc->_localDirty = true;
        // tc->markWorldDirty();

        // YA_CORE_TRACE("Gizmo manipulated: local pos({}, {}, {})", position.x, position.y, position.z);
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

    const auto& frameState = app->getRenderServices().getRenderFrameState();
    glm::mat4   view       = frameState.view;
    glm::mat4   projection = frameState.projection;

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

    // Update selection
    if (pickedEntity) {
        // Ctrl/Cmd toggles membership, Shift extends from the anchor; plain
        // clicks replace the selection.
        _sceneHierarchyPanel.handleEntityClick(pickedEntity);
        YA_CORE_INFO("Picked entity: {}", pickedEntity->getName());
    }
    else {
        _sceneHierarchyPanel.setSelection(nullptr);
        YA_CORE_INFO("No entity picked");
    }
}

void EditorLayer::pickNode2D(float viewportLocalX, float viewportLocalY)
{
    glm::vec2 canvasPoint{viewportLocalX, viewportLocalY};
    if (!viewportToCanvas(canvasPoint, canvasPoint)) {
        _uiDesignerPanel.clearSelection();
        return;
    }

    // Game UI picking hits the UI Designer's preview tree (the authoring fact
    // source); scene entries are runtime data instantiated by GameUIHost.
    if (UIElement* picked = _uiDesignerPanel.pickAt(canvasPoint)) {
        _uiDesignerPanel.select(picked);
        YA_CORE_INFO("Picked UI widget: {}", picked->_name);
        return;
    }

    _uiDesignerPanel.clearSelection();
}

// === 2D canvas direct manipulation (designer preview) ===

uint8_t EditorLayer::hitTestCanvasResizeHandles(const UIElement& widget) const
{
    glm::vec2 vpMouse{};
    if (!screenToViewport(_app->getLastMousePos(), vpMouse)) {
        return 0;
    }
    const Rect2D vpRect{
        .pos    = widget._layoutRect.pos * _canvasZoom + _canvasPan,
        .extent = widget._layoutRect.extent * _canvasZoom,
    };

    // Handles sit at the four corners and the four edge midpoints, drawn as
    // fixed screen-size squares regardless of zoom (same space as the
    // selection overlay in the canvas compose).
    constexpr float kHalf = 5.0f; // generous grab box (10x10 px)
    const glm::vec2 corners[4] = {
        {vpRect.pos.x,                          vpRect.pos.y},
        {vpRect.pos.x + vpRect.extent.x,        vpRect.pos.y},
        {vpRect.pos.x,                          vpRect.pos.y + vpRect.extent.y},
        {vpRect.pos.x + vpRect.extent.x,        vpRect.pos.y + vpRect.extent.y},
    };
    const glm::vec2 edges[4] = {
        {vpRect.pos.x + vpRect.extent.x * 0.5f, vpRect.pos.y},                       // top
        {vpRect.pos.x + vpRect.extent.x * 0.5f, vpRect.pos.y + vpRect.extent.y},     // bottom
        {vpRect.pos.x,                          vpRect.pos.y + vpRect.extent.y * 0.5f}, // left
        {vpRect.pos.x + vpRect.extent.x,        vpRect.pos.y + vpRect.extent.y * 0.5f}, // right
    };
    const auto hit = [&](const glm::vec2& p) {
        return std::fabs(vpMouse.x - p.x) <= kHalf && std::fabs(vpMouse.y - p.y) <= kHalf;
    };

    uint8_t mask = 0;
    if (hit(corners[0])) mask |= UIDesignerPanel::kResizeHandleLeft | UIDesignerPanel::kResizeHandleTop;
    if (hit(corners[1])) mask |= UIDesignerPanel::kResizeHandleRight | UIDesignerPanel::kResizeHandleTop;
    if (hit(corners[2])) mask |= UIDesignerPanel::kResizeHandleLeft | UIDesignerPanel::kResizeHandleBottom;
    if (hit(corners[3])) mask |= UIDesignerPanel::kResizeHandleRight | UIDesignerPanel::kResizeHandleBottom;
    if (hit(edges[0])) mask |= UIDesignerPanel::kResizeHandleTop;
    if (hit(edges[1])) mask |= UIDesignerPanel::kResizeHandleBottom;
    if (hit(edges[2])) mask |= UIDesignerPanel::kResizeHandleLeft;
    if (hit(edges[3])) mask |= UIDesignerPanel::kResizeHandleRight;
    return mask;
}

void EditorLayer::beginCanvasPress()
{
    _canvasPressHit     = nullptr;
    _canvasPressPoint   = {0.0f, 0.0f};
    _bCanvasPressActive = true;

    glm::vec2 vpLocal{};
    if (!screenToViewport(_app->getLastMousePos(), vpLocal)) {
        return;
    }
    glm::vec2 canvasPoint = vpLocal;
    if (!viewportToCanvas(canvasPoint, canvasPoint)) {
        // Outside the visible canvas region: treat as empty.
        _uiDesignerPanel.clearSelection();
        return;
    }
    _canvasPressPoint = canvasPoint;

    // 1) Resize handles of the current selection take priority over picking
    //    (grab the edge/corner without de-selecting the widget).
    if (UIElement* selected = _uiDesignerPanel.getSelectedWidget()) {
        if (const uint8_t mask = hitTestCanvasResizeHandles(*selected)) {
            _canvasPressHit = selected;
            _uiDesignerPanel.beginResize(selected, canvasPoint, mask);
            return;
        }
    }

    // 2) Hit the preview tree: select on press and start a move session.
    if (UIElement* picked = _uiDesignerPanel.pickAt(canvasPoint)) {
        _uiDesignerPanel.select(picked);
        _canvasPressHit = picked;
        _uiDesignerPanel.beginMove(picked, canvasPoint);
        YA_CORE_INFO("Picked UI widget: {}", picked->_name);
        return;
    }

    // 3) Empty canvas: clear the selection.
    _uiDesignerPanel.clearSelection();
}

void EditorLayer::updateCanvasDrag()
{
    if (!_canvasPressHit) {
        return;
    }
    glm::vec2 vpLocal{};
    if (!screenToViewport(_app->getLastMousePos(), vpLocal)) {
        return;
    }
    glm::vec2 canvasPoint = vpLocal;
    if (!viewportToCanvas(canvasPoint, canvasPoint)) {
        return;
    }
    if (_uiDesignerPanel.isDragging(_canvasPressHit)) {
        if (!_uiDesignerPanel.applyDragDelta(canvasPoint - _canvasPressPoint)) {
            endCanvasPress();
        }
    }
}

void EditorLayer::endCanvasPress()
{
    _canvasPressHit     = nullptr;
    _canvasPressPoint   = {0.0f, 0.0f};
    _bCanvasPressActive = false;
    _uiDesignerPanel.endDrag();
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
    if (glm::length2(camToEntity) < 1e-6f) {
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
