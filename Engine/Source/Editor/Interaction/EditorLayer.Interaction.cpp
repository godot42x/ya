#include "Editor/EditorLayerInternal.h"

namespace ya
{
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
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && bViewportHovered) {
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
            // Reset drag state on release (after a short delay to let ImGui process)
            // We keep the flag true briefly so context menu check can see it
            Facade.timerManager.delayCall(50, [this]() {
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
            // Focus camera on selected entity
            if (auto* selectedEntity = _sceneHierarchyPanel.getSelectedEntity(); selectedEntity && selectedEntity->isValid()) {
                focusCameraOnEntity(_sceneHierarchyPanel.getSelectedEntity());
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
    // Get selected entity from hierarchy panel
    Entity* selectedEntity = _sceneHierarchyPanel.getSelectedEntity();

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

    const auto& frameState = app->getRenderFrameState();
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
    glm::mat4 worldTransform = tc->getTransform();

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

    const auto& frameState = app->getRenderFrameState();
    glm::mat4   view       = frameState.view;
    glm::mat4   projection = frameState.projection;

    // Use RayCastMousePickingSystem to pick entity
    // viewportLocalX/Y are in viewport space (0,0 = top-left of viewport)
    Entity* pickedEntity = RayCastMousePickingSystem::pickEntity(
        scene,
        viewportLocalX,
        viewportLocalY,
        _viewportSize.x,
        _viewportSize.y,
        view,
        projection);

    // Update selection
    if (pickedEntity) {
        _sceneHierarchyPanel.setSelection(pickedEntity);
        YA_CORE_INFO("Picked entity: {}", pickedEntity->getName());
    }
    else {
        _sceneHierarchyPanel.setSelection(nullptr);
        YA_CORE_INFO("No entity picked");
    }
}

void EditorLayer::focusCameraOnEntity(Entity* entity)
{
    if (!entity || !entity->isValid()) {
        return;
    }
    auto* tc = entity->getComponent<TransformComponent>();
    if (!tc) {
        return;
    }
    auto* app = App::get();
    if (!app) {
        return;
    }
    float distance = 10.0f; // Fixed distance from entity, can be adjusted

    glm::vec3 entityPos = tc->getPosition();
    glm::vec3 camPos    = _camera.getPosition();

    glm::vec3 camToEntity = glm::normalize(entityPos - camPos);

    // Position camera behind entity at fixed distance
    glm::vec3 newCamPos = entityPos - camToEntity * distance;

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
