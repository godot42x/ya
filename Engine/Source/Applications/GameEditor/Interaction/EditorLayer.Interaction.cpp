#include "GameEditor/EditorLayerInternal.h"
#include "GameEditor/UI/EditorTransformUndo.h"

#include "Core/Math/Ray.h"
#include "ECS/System/RayCastMousePickingSystem.h"
#include "ECS/Systems/TransformSystem.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Render.h"
#include "Render2D/Render2D.h"

#include <array>
#include <cmath>
#include <functional>
#include <glm/gtx/matrix_decompose.hpp>
#include <limits>
#include <numbers>
#include <optional>

namespace ya
{

namespace
{
void resetGizmoUndoSession(bool& active, std::vector<FEditorTransformSnapshot>& before)
{
    before.clear();
    active = false;
}

constexpr float kViewportGizmoAxisPixels      = 90.0f;
constexpr float kViewportGizmoRingPixels      = 64.0f;
constexpr float kViewportGizmoLineHitPixels   = 10.0f;
constexpr float kViewportGizmoHandlePixels    = 9.0f;
constexpr float kViewportGizmoScaleMinAbs     = 0.05f;
constexpr float kViewportGizmoTranslateSnap   = 0.5f;
constexpr float kViewportGizmoRotateSnapDeg   = 15.0f;
constexpr float kViewportGizmoScaleSnap       = 0.1f;
constexpr int   kViewportGizmoRingSegments    = 48;

struct FGizmoAxisFrame
{
    EEditorViewportGizmoAxis axis = EEditorViewportGizmoAxis::None;
    glm::vec3                worldDir{0.0f, 0.0f, 0.0f};
    glm::vec3                worldEnd{0.0f, 0.0f, 0.0f};
    glm::vec2                screenEnd{0.0f, 0.0f};
    bool                     bProjected = false;
};

struct FViewportGizmoFrame
{
    Entity*    entity    = nullptr;
    uint64_t   entityUuid = 0;
    glm::vec3  originWorld{0.0f, 0.0f, 0.0f};
    glm::quat  rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3  scale{1.0f, 1.0f, 1.0f};
    glm::vec3  cameraWorld{0.0f, 0.0f, 0.0f};
    glm::vec2  originScreen{0.0f, 0.0f};
    float      axisLengthWorld = 1.0f;
    float      ringRadiusWorld = 1.0f;
    std::array<FGizmoAxisFrame, 3> axes{};
};

glm::vec3 gizmoAxisBasis(EEditorViewportGizmoAxis axis)
{
    switch (axis) {
    case EEditorViewportGizmoAxis::X:
        return {1.0f, 0.0f, 0.0f};
    case EEditorViewportGizmoAxis::Y:
        return {0.0f, 1.0f, 0.0f};
    case EEditorViewportGizmoAxis::Z:
        return {0.0f, 0.0f, 1.0f};
    case EEditorViewportGizmoAxis::None:
    default:
        return {0.0f, 0.0f, 0.0f};
    }
}

size_t gizmoAxisIndex(EEditorViewportGizmoAxis axis)
{
    switch (axis) {
    case EEditorViewportGizmoAxis::X:
        return 0;
    case EEditorViewportGizmoAxis::Y:
        return 1;
    case EEditorViewportGizmoAxis::Z:
        return 2;
    case EEditorViewportGizmoAxis::None:
    default:
        return 0;
    }
}

glm::vec4 gizmoAxisColor(EEditorViewportGizmoAxis axis, bool highlighted)
{
    const glm::vec4 tint = highlighted ? glm::vec4(1.0f, 0.95f, 0.72f, 1.0f) : glm::vec4(1.0f);
    switch (axis) {
    case EEditorViewportGizmoAxis::X:
        return glm::vec4(0.96f, 0.24f, 0.24f, 1.0f) * tint;
    case EEditorViewportGizmoAxis::Y:
        return glm::vec4(0.25f, 0.88f, 0.34f, 1.0f) * tint;
    case EEditorViewportGizmoAxis::Z:
        return glm::vec4(0.28f, 0.58f, 1.0f, 1.0f) * tint;
    case EEditorViewportGizmoAxis::None:
    default:
        return highlighted ? glm::vec4(1.0f, 0.95f, 0.72f, 1.0f) : glm::vec4(0.85f, 0.85f, 0.85f, 1.0f);
    }
}

float snapScalar(float value, float step)
{
    if (step <= 0.0f) {
        return value;
    }
    return std::round(value / step) * step;
}

float clampScaleValue(float value)
{
    if (value >= 0.0f) {
        return std::max(value, kViewportGizmoScaleMinAbs);
    }
    return std::min(value, -kViewportGizmoScaleMinAbs);
}

bool decomposeTrs(const glm::mat4& world, glm::vec3& position, glm::quat& rotation, glm::vec3& scale)
{
    glm::vec3 skew;
    glm::vec4 perspective;
    if (!glm::decompose(world, scale, rotation, position, skew, perspective)) {
        return false;
    }
    rotation = glm::normalize(rotation);
    return true;
}

glm::mat4 composeTrs(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale)
{
    return glm::translate(glm::mat4(1.0f), position) *
           glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

bool projectWorldToViewport(const FEditorViewportHostState& host,
                            const glm::vec3&               world,
                            glm::vec2&                     outScreen)
{
    const glm::vec4 clip = host.projection * host.view * glm::vec4(world, 1.0f);
    if (std::abs(clip.w) <= 1e-5f || clip.z <= -clip.w) {
        return false;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    outScreen.x = (ndc.x * 0.5f + 0.5f) * host.extent.x;
    outScreen.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * host.extent.y;
    return std::isfinite(outScreen.x) && std::isfinite(outScreen.y);
}

float computeWorldUnitsPerPixel(const FEditorViewportHostState& host, const glm::vec3& world)
{
    const glm::vec3 viewSpace = glm::vec3(host.view * glm::vec4(world, 1.0f));
    const float     depth     = std::max(std::abs(viewSpace.z), 0.05f);
    const float     scaleY    = std::abs(host.projection[1][1]);
    if (scaleY <= 1e-5f || host.extent.y <= 1.0f) {
        return 0.01f;
    }
    return (2.0f * depth) / (scaleY * host.extent.y);
}

Ray makeViewportRay(const FEditorViewportHostState& host, const glm::vec2& localPoint)
{
    return Ray::fromScreen(localPoint.x,
                           localPoint.y,
                           std::max(host.extent.x, 1.0f),
                           std::max(host.extent.y, 1.0f),
                           host.view,
                           host.projection);
}

std::optional<glm::vec3> intersectRayPlane(const Ray& ray,
                                           const glm::vec3& planePoint,
                                           const glm::vec3& planeNormal)
{
    const float denom = glm::dot(ray.direction, planeNormal);
    if (std::abs(denom) <= 1e-5f) {
        return std::nullopt;
    }
    const float t = glm::dot(planePoint - ray.origin, planeNormal) / denom;
    if (t < 0.0f) {
        return std::nullopt;
    }
    return ray.at(t);
}

glm::vec3 choosePerpendicular(const glm::vec3& normal)
{
    glm::vec3 axis = glm::cross(normal, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::dot(axis, axis) <= 1e-5f) {
        axis = glm::cross(normal, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    if (glm::dot(axis, axis) <= 1e-5f) {
        axis = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    return glm::normalize(axis);
}

glm::vec3 buildAxisDragPlaneNormal(const glm::vec3& axis, const glm::vec3& cameraToOrigin)
{
    const glm::vec3 tangent = glm::cross(cameraToOrigin, axis);
    glm::vec3 planeNormal   = glm::cross(axis, tangent);
    if (glm::dot(planeNormal, planeNormal) <= 1e-5f) {
        planeNormal = choosePerpendicular(axis);
    }
    return glm::normalize(planeNormal);
}

float signedAngleAroundAxis(const glm::vec3& from,
                            const glm::vec3& to,
                            const glm::vec3& axis)
{
    const glm::vec3 crossValue = glm::cross(from, to);
    return std::atan2(glm::dot(crossValue, axis), glm::dot(from, to));
}

float distanceSquaredToSegment(const glm::vec2& point,
                               const glm::vec2& start,
                               const glm::vec2& end,
                               float*           outT = nullptr)
{
    const glm::vec2 segment  = end - start;
    const float     lengthSq = glm::dot(segment, segment);
    float           t        = 0.0f;
    if (lengthSq > 1e-5f) {
        t = glm::clamp(glm::dot(point - start, segment) / lengthSq, 0.0f, 1.0f);
    }
    if (outT) {
        *outT = t;
    }
    const glm::vec2 projected = start + segment * t;
    const glm::vec2 delta     = point - projected;
    return glm::dot(delta, delta);
}

std::optional<FViewportGizmoFrame> buildViewportGizmoFrame(const EditorLayer&              layer,
                                                           const FEditorViewportHostState& host,
                                                           EEditorViewportGizmoMode        mode)
{
    Entity* selectedEntity = layer.getSelectedEntity();
    if (!selectedEntity || !selectedEntity->isValid() ||
        !selectedEntity->hasComponent<TransformComponent>()) {
        return std::nullopt;
    }

    auto* transform = selectedEntity->getComponent<TransformComponent>();
    auto* id        = selectedEntity->getComponent<IDComponent>();
    if (!transform || !id) {
        return std::nullopt;
    }

    FViewportGizmoFrame frame;
    frame.entity      = selectedEntity;
    frame.entityUuid  = id->_id.value;
    const glm::mat4 world = transform->getTransform();
    if (!decomposeTrs(world, frame.originWorld, frame.rotation, frame.scale)) {
        return std::nullopt;
    }
    if (!projectWorldToViewport(host, frame.originWorld, frame.originScreen)) {
        return std::nullopt;
    }

    frame.cameraWorld = glm::vec3(glm::inverse(host.view)[3]);
    const float worldPerPixel = computeWorldUnitsPerPixel(host, frame.originWorld);
    frame.axisLengthWorld = std::max(worldPerPixel * kViewportGizmoAxisPixels, 0.2f);
    frame.ringRadiusWorld = std::max(worldPerPixel * kViewportGizmoRingPixels, 0.15f);

    constexpr std::array<EEditorViewportGizmoAxis, 3> axes = {
        EEditorViewportGizmoAxis::X,
        EEditorViewportGizmoAxis::Y,
        EEditorViewportGizmoAxis::Z,
    };
    for (size_t i = 0; i < axes.size(); ++i) {
        FGizmoAxisFrame axisFrame;
        axisFrame.axis = axes[i];
        const glm::vec3 basis = gizmoAxisBasis(axisFrame.axis);
        axisFrame.worldDir =
            mode == EEditorViewportGizmoMode::Local ? glm::normalize(frame.rotation * basis) : basis;
        axisFrame.worldEnd = frame.originWorld + axisFrame.worldDir * frame.axisLengthWorld;
        axisFrame.bProjected = projectWorldToViewport(host, axisFrame.worldEnd, axisFrame.screenEnd);
        frame.axes[i] = axisFrame;
    }
    return frame;
}

EEditorViewportGizmoAxis hitTestLinearAxis(const FViewportGizmoFrame& frame, const glm::vec2& point)
{
    EEditorViewportGizmoAxis bestAxis = EEditorViewportGizmoAxis::None;
    float                    bestDistSq = kViewportGizmoLineHitPixels * kViewportGizmoLineHitPixels;
    for (const auto& axis : frame.axes) {
        if (!axis.bProjected) {
            continue;
        }
        float t = 0.0f;
        const float distSq = distanceSquaredToSegment(point, frame.originScreen, axis.screenEnd, &t);
        if (t < 0.18f || t > 1.05f || distSq > bestDistSq) {
            continue;
        }
        bestAxis   = axis.axis;
        bestDistSq = distSq;
    }
    return bestAxis;
}

EEditorViewportGizmoAxis hitTestRotateAxis(const FEditorViewportHostState& host,
                                           const FViewportGizmoFrame&      frame,
                                           const glm::vec2&                point)
{
    const Ray ray = makeViewportRay(host, point);
    EEditorViewportGizmoAxis bestAxis = EEditorViewportGizmoAxis::None;
    float                    bestDistance = std::numeric_limits<float>::max();
    for (const auto& axis : frame.axes) {
        const auto hit = intersectRayPlane(ray, frame.originWorld, axis.worldDir);
        if (!hit.has_value()) {
            continue;
        }
        const float radiusError = std::abs(glm::length(*hit - frame.originWorld) - frame.ringRadiusWorld);
        const float tolerance   = std::max(frame.ringRadiusWorld * 0.18f, frame.axisLengthWorld * 0.12f);
        if (radiusError > tolerance || radiusError >= bestDistance) {
            continue;
        }
        bestAxis     = axis.axis;
        bestDistance = radiusError;
    }
    return bestAxis;
}

bool isViewportGizmoSnapEnabled(const App& app)
{
#if defined(__APPLE__)
    return app.getInputManager().isKeyPressed(EKey::LMeta) ||
           app.getInputManager().isKeyPressed(EKey::RMeta);
#else
    return app.getInputManager().isKeyPressed(EKey::LCtrl) ||
           app.getInputManager().isKeyPressed(EKey::RCtrl);
#endif
}

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
        if (_app && _app->getInputManager().isMouseButtonDown(EMouse::Right) && bViewportHovered) {
            glm::vec2 currentPos = _app->getLastMousePos();
            float     dist       = glm::length(currentPos - _rightMousePressPos);
            if (dist > 3.0f) { // Threshold to distinguish click from drag
                _bRightMouseDragging = true;
            }
        }

        // 2D canvas panning (right or middle drag).
        if (isViewportMode2D() && bViewportHovered) {
            const bool bPanning = _app &&
                                  (_app->getInputManager().isMouseButtonDown(EMouse::Right) ||
                                   _app->getInputManager().isMouseButtonDown(EMouse::Middle));
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
            _app && _app->getInputManager().isMouseButtonDown(EMouse::Left) &&
            !_app->getInputManager().isMouseButtonDown(EMouse::Right) &&
            !_app->getInputManager().isMouseButtonDown(EMouse::Middle)) {
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
            if (_bViewportGizmoConsumeReleasePick) {
                _bViewportGizmoConsumeReleasePick = false;
                break;
            }
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
                setViewportGizmoOperation(EEditorViewportGizmoOperation::Translate);
                break;
            case EKey::K_E:
                setViewportGizmoOperation(EEditorViewportGizmoOperation::Rotate);
                break;
            case EKey::K_R:
                setViewportGizmoOperation(EEditorViewportGizmoOperation::Scale);
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

bool EditorLayer::isGizmoActive() const
{
    return _bViewportGizmoDragging || _bViewportGizmoHovered;
}

bool EditorLayer::hasViewportGizmoSelection() const
{
    if (!_bViewportGizmoHostValid || isViewportMode2D()) {
        return false;
    }
    Entity* selectedEntity = getSelectedEntity();
    return selectedEntity && selectedEntity->isValid() &&
           selectedEntity->hasComponent<TransformComponent>() &&
           selectedEntity->getComponent<IDComponent>() != nullptr;
}

void EditorLayer::syncViewportGizmoHost(const FEditorViewportHostState& host)
{
    _viewportGizmoHostState  = host;
    _bViewportGizmoHostValid = host.extent.x > 0.0f && host.extent.y > 0.0f;
    if (!_bViewportGizmoHostValid) {
        cancelViewportGizmoDrag();
        _bViewportGizmoHovered = false;
        _gizmoHoveredAxis      = EEditorViewportGizmoAxis::None;
        return;
    }
    if (!host.bHovered && !_bViewportGizmoDragging) {
        _bViewportGizmoPointerInside = false;
        _bViewportGizmoHovered       = false;
        _gizmoHoveredAxis            = EEditorViewportGizmoAxis::None;
    }
}

void EditorLayer::setViewportGizmoPointer(const glm::vec2& localPoint, bool insideViewport)
{
    _viewportGizmoPointerLocal  = localPoint;
    _bViewportGizmoPointerInside = insideViewport;
    if (_bViewportGizmoDragging) {
        updateViewportGizmoDrag(localPoint);
        return;
    }
    if (!insideViewport || !hasViewportGizmoSelection()) {
        _bViewportGizmoHovered = false;
        _gizmoHoveredAxis      = EEditorViewportGizmoAxis::None;
        return;
    }

    const auto frame = buildViewportGizmoFrame(*this, _viewportGizmoHostState, _gizmoMode);
    if (!frame.has_value()) {
        _bViewportGizmoHovered = false;
        _gizmoHoveredAxis      = EEditorViewportGizmoAxis::None;
        return;
    }

    _gizmoHoveredAxis = _gizmoOperation == EEditorViewportGizmoOperation::Rotate
                            ? hitTestRotateAxis(_viewportGizmoHostState, *frame, localPoint)
                            : hitTestLinearAxis(*frame, localPoint);
    _bViewportGizmoHovered = _gizmoHoveredAxis != EEditorViewportGizmoAxis::None;
}

bool EditorLayer::beginViewportGizmoDrag(const glm::vec2& localPoint)
{
    setViewportGizmoPointer(localPoint, true);
    if (_bViewportGizmoDragging || !hasViewportGizmoSelection()) {
        return false;
    }

    const auto frame = buildViewportGizmoFrame(*this, _viewportGizmoHostState, _gizmoMode);
    if (!frame.has_value()) {
        return false;
    }

    const EEditorViewportGizmoAxis axis =
        _gizmoOperation == EEditorViewportGizmoOperation::Rotate
            ? hitTestRotateAxis(_viewportGizmoHostState, *frame, localPoint)
            : hitTestLinearAxis(*frame, localPoint);
    if (axis == EEditorViewportGizmoAxis::None) {
        return false;
    }

    const FGizmoAxisFrame& axisFrame = frame->axes[gizmoAxisIndex(axis)];
    const Ray              ray       = makeViewportRay(_viewportGizmoHostState, localPoint);
    const glm::vec3 cameraToOrigin = glm::normalize(frame->originWorld - frame->cameraWorld);

    _gizmoActiveAxis             = axis;
    _gizmoHoveredAxis            = axis;
    _bViewportGizmoHovered       = true;
    _bViewportGizmoDragging      = true;
    _bViewportGizmoConsumeReleasePick = true;
    _gizmoDragStartPrimaryWorld  = frame->entity->getComponent<TransformComponent>()->getTransform();
    _gizmoDragAxisWorld          = axisFrame.worldDir;
    _gizmoDragOriginWorld        = frame->originWorld;
    _gizmoUndoBefore             = captureEditorTransformSelection(getSelections());

    if (_gizmoOperation == EEditorViewportGizmoOperation::Rotate) {
        _gizmoDragPlaneNormal = axisFrame.worldDir;
        const auto hit = intersectRayPlane(ray, frame->originWorld, _gizmoDragPlaneNormal);
        if (!hit.has_value()) {
            cancelViewportGizmoDrag();
            return false;
        }
        const glm::vec3 fromOrigin = *hit - frame->originWorld;
        if (glm::dot(fromOrigin, fromOrigin) <= 1e-5f) {
            cancelViewportGizmoDrag();
            return false;
        }
        _gizmoDragStartPlaneVector = glm::normalize(fromOrigin);
        _gizmoDragStartScalar      = 0.0f;
        return true;
    }

    _gizmoDragPlaneNormal = buildAxisDragPlaneNormal(axisFrame.worldDir, cameraToOrigin);
    const auto hit = intersectRayPlane(ray, frame->originWorld, _gizmoDragPlaneNormal);
    if (!hit.has_value()) {
        cancelViewportGizmoDrag();
        return false;
    }
    _gizmoDragStartScalar = glm::dot(*hit - frame->originWorld, axisFrame.worldDir);
    _gizmoDragStartPlaneVector = glm::vec3(0.0f);
    return true;
}

void EditorLayer::updateViewportGizmoDrag(const glm::vec2& localPoint)
{
    if (!_bViewportGizmoDragging || !_app) {
        return;
    }

    Entity* selectedEntity = getSelectedEntity();
    if (!selectedEntity || !selectedEntity->isValid() || !selectedEntity->hasComponent<TransformComponent>()) {
        cancelViewportGizmoDrag();
        return;
    }

    glm::vec3 startPosition;
    glm::quat startRotation;
    glm::vec3 startScale;
    if (!decomposeTrs(_gizmoDragStartPrimaryWorld, startPosition, startRotation, startScale)) {
        cancelViewportGizmoDrag();
        return;
    }

    const Ray ray = makeViewportRay(_viewportGizmoHostState, localPoint);
    glm::mat4 newPrimaryWorld = _gizmoDragStartPrimaryWorld;

    if (_gizmoOperation == EEditorViewportGizmoOperation::Rotate) {
        const auto hit = intersectRayPlane(ray, _gizmoDragOriginWorld, _gizmoDragPlaneNormal);
        if (!hit.has_value()) {
            return;
        }
        glm::vec3 planeVector = *hit - _gizmoDragOriginWorld;
        if (glm::dot(planeVector, planeVector) <= 1e-5f) {
            return;
        }
        planeVector = glm::normalize(planeVector);
        float angle = signedAngleAroundAxis(_gizmoDragStartPlaneVector, planeVector, _gizmoDragPlaneNormal);
        if (isViewportGizmoSnapEnabled(*_app)) {
            angle = glm::radians(snapScalar(glm::degrees(angle), kViewportGizmoRotateSnapDeg));
        }
        newPrimaryWorld = composeTrs(startPosition, glm::angleAxis(angle, _gizmoDragPlaneNormal) * startRotation, startScale);
    }
    else {
        const auto hit = intersectRayPlane(ray, _gizmoDragOriginWorld, _gizmoDragPlaneNormal);
        if (!hit.has_value()) {
            return;
        }
        float delta = glm::dot(*hit - _gizmoDragOriginWorld, _gizmoDragAxisWorld) - _gizmoDragStartScalar;
        if (_gizmoOperation == EEditorViewportGizmoOperation::Translate) {
            if (isViewportGizmoSnapEnabled(*_app)) {
                delta = snapScalar(delta, kViewportGizmoTranslateSnap);
            }
            newPrimaryWorld = composeTrs(startPosition + _gizmoDragAxisWorld * delta, startRotation, startScale);
        }
        else {
            const float axisLengthWorld =
                std::max(computeWorldUnitsPerPixel(_viewportGizmoHostState, _gizmoDragOriginWorld) *
                             kViewportGizmoAxisPixels,
                         0.2f);
            float scaleDelta = delta / axisLengthWorld;
            if (isViewportGizmoSnapEnabled(*_app)) {
                scaleDelta = snapScalar(scaleDelta, kViewportGizmoScaleSnap);
            }
            glm::vec3 newScale = startScale;
            const size_t axisIndex = gizmoAxisIndex(_gizmoActiveAxis);
            newScale[axisIndex] = clampScaleValue(startScale[axisIndex] + scaleDelta);
            newPrimaryWorld = composeTrs(startPosition, startRotation, newScale);
        }
    }

    auto* primaryTransform = selectedEntity->getComponent<TransformComponent>();
    TransformSystem::setWorldTransform(primaryTransform, newPrimaryWorld);

    Scene* scene = getViewportInteractionScene();
    if (scene && _gizmoUndoBefore.size() > 1) {
        const glm::mat4 deltaWorld = newPrimaryWorld * glm::inverse(_gizmoDragStartPrimaryWorld);
        const uint64_t primaryUuid = _gizmoUndoBefore.empty() ? 0 : _gizmoUndoBefore.front().entityUUID;
        for (const auto& snapshot : _gizmoUndoBefore) {
            if (snapshot.entityUUID == primaryUuid) {
                continue;
            }
            Entity* other = scene->getEntityByUUID(snapshot.entityUUID);
            if (!other || !other->isValid() || !other->hasComponent<TransformComponent>()) {
                continue;
            }
            TransformSystem::setWorldTransform(other->getComponent<TransformComponent>(), deltaWorld * snapshot.world);
        }
    }
}

void EditorLayer::endViewportGizmoDrag()
{
    if (!_bViewportGizmoDragging) {
        return;
    }
    Scene* scene = getViewportInteractionScene();
    std::vector<FEditorTransformSnapshot> after = captureEditorTransformSelection(getSelections());
    if (_gizmoUndo && scene && after.size() == _gizmoUndoBefore.size()) {
        (void)pushEditorTransformUndo(*_gizmoUndo, scene, _gizmoUndoBefore, std::move(after));
    }
    _bViewportGizmoDragging = false;
    _gizmoActiveAxis        = EEditorViewportGizmoAxis::None;
    _bViewportGizmoConsumeReleasePick = true;
    _gizmoUndoBefore.clear();
    setViewportGizmoPointer(_viewportGizmoPointerLocal, _bViewportGizmoPointerInside);
}

void EditorLayer::cancelViewportGizmoDrag()
{
    _bViewportGizmoDragging = false;
    _bViewportGizmoHovered  = false;
    _gizmoActiveAxis        = EEditorViewportGizmoAxis::None;
    _gizmoHoveredAxis       = EEditorViewportGizmoAxis::None;
    _bViewportGizmoConsumeReleasePick = false;
    _gizmoUndoBefore.clear();
}

void EditorLayer::setViewportGizmoOperation(EEditorViewportGizmoOperation operation)
{
    _gizmoOperation = operation;
    if (_bViewportGizmoPointerInside && !_bViewportGizmoDragging) {
        setViewportGizmoPointer(_viewportGizmoPointerLocal, true);
    }
}

void EditorLayer::recordViewportGizmoOverlay() const
{
    YA_PROFILE_FUNCTION();
    if (!hasViewportGizmoSelection()) {
        return;
    }

    const auto frame = buildViewportGizmoFrame(*this, _viewportGizmoHostState, _gizmoMode);
    if (!frame.has_value()) {
        return;
    }

    const EEditorViewportGizmoAxis highlightedAxis =
        _bViewportGizmoDragging ? _gizmoActiveAxis : _gizmoHoveredAxis;
    auto* white = TextureLibrary::get().getWhiteTexture().get();

    if (_gizmoOperation == EEditorViewportGizmoOperation::Rotate) {
        for (const auto& axis : frame->axes) {
            const bool highlighted = axis.axis == highlightedAxis;
            const glm::vec4 color  = gizmoAxisColor(axis.axis, highlighted);
            const glm::vec3 tangent0 = choosePerpendicular(axis.worldDir);
            const glm::vec3 tangent1 = glm::normalize(glm::cross(axis.worldDir, tangent0));
            glm::vec3 prev = frame->originWorld + tangent0 * frame->ringRadiusWorld;
            for (int segment = 1; segment <= kViewportGizmoRingSegments; ++segment) {
                const float angle = (2.0f * std::numbers::pi_v<float>) * static_cast<float>(segment) /
                                    static_cast<float>(kViewportGizmoRingSegments);
                const glm::vec3 next =
                    frame->originWorld +
                    (tangent0 * std::cos(angle) + tangent1 * std::sin(angle)) * frame->ringRadiusWorld;
                Render2D::makeWorldLine(prev, next, color);
                prev = next;
            }
        }
        return;
    }

    if (!white) {
        return;
    }

    for (const auto& axis : frame->axes) {
        if (!axis.bProjected) {
            continue;
        }
        const bool highlighted = axis.axis == highlightedAxis;
        const glm::vec4 color  = gizmoAxisColor(axis.axis, highlighted);
        Render2D::makeWorldLine(frame->originWorld, axis.worldEnd, color);
        Render2D::makeSprite(glm::vec3(axis.screenEnd.x - kViewportGizmoHandlePixels * 0.5f,
                                       axis.screenEnd.y - kViewportGizmoHandlePixels * 0.5f,
                                       0.0f),
                             glm::vec2(kViewportGizmoHandlePixels, kViewportGizmoHandlePixels),
                             white,
                             color);
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
