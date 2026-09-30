#include "GameEditor/UI/Viewport/EditorViewportGizmoController.h"

#include "Core/Input/InputManager.h"
#include "Core/KeyCode.h"
#include "Core/Math/Ray.h"
#include "Core/Profiling/Instrumentor.h"
#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "ECS/Systems/TransformSystem.h"
#include "GameEditor/UI/Ops/EditorTransformUndo.h"
#include "Render/Adapters/Companion/CompanionManager.h"
#include "GameRuntime/App.h"
#include "RHI/Backend/TextureLibrary.h"
#include "Render2D/ScreenDrawList.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <limits>
#include <numbers>
#include <optional>

namespace ya
{

namespace
{

constexpr float kViewportGizmoAxisPixels    = 90.0f;
constexpr float kViewportGizmoRingPixels    = 64.0f;
constexpr float kViewportGizmoLineHitPixels = 10.0f;
constexpr float kViewportGizmoHandlePixels  = 9.0f;
constexpr float kViewportGizmoScaleMinAbs   = 0.05f;
constexpr float kViewportGizmoTranslateSnap = 0.5f;
constexpr float kViewportGizmoRotateSnapDeg = 15.0f;
constexpr float kViewportGizmoScaleSnap     = 0.1f;
constexpr int   kViewportGizmoRingSegments  = 48;

const std::vector<Entity*> kEmptyGizmoSelection;

struct FGizmoAxisFrame
{
    EEditorViewportGizmoAxis axis = EEditorViewportGizmoAxis::None;
    glm::vec3                worldDir{0.0f, 0.0f, 0.0f};
    glm::vec3                worldEnd{0.0f, 0.0f, 0.0f};
    glm::vec2                screenEnd{0.0f, 0.0f};
    bool                     bProjected = false;
    bool                     bReversed  = false;
};

struct FViewportGizmoFrame
{
    Entity*    entity     = nullptr;
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

glm::vec4 gizmoAxisColor(EEditorViewportGizmoAxis axis, bool highlighted, bool pressed)
{
    glm::vec4 base{0.85f, 0.85f, 0.85f, 1.0f};
    switch (axis) {
    case EEditorViewportGizmoAxis::X:
        base = {0.96f, 0.24f, 0.24f, 1.0f};
        break;
    case EEditorViewportGizmoAxis::Y:
        base = {0.25f, 0.88f, 0.34f, 1.0f};
        break;
    case EEditorViewportGizmoAxis::Z:
        base = {0.28f, 0.58f, 1.0f, 1.0f};
        break;
    case EEditorViewportGizmoAxis::None:
    default:
        break;
    }
    if (pressed) {
        return glm::mix(base, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.60f);
    }
    if (highlighted) {
        return glm::mix(base, glm::vec4(1.0f, 0.92f, 0.22f, 1.0f), 0.55f);
    }
    return base;
}

float gizmoLineThickness(bool highlighted, bool pressed)
{
    if (pressed) {
        return 3.5f;
    }
    if (highlighted) {
        return 3.0f;
    }
    return 2.0f;
}

float gizmoHandlePixels(bool highlighted, bool pressed)
{
    if (pressed) {
        return 12.0f;
    }
    if (highlighted) {
        return 11.0f;
    }
    return kViewportGizmoHandlePixels;
}

void drawScreenLine(ScreenDrawList& list, const glm::vec2& from,
                    const glm::vec2& to,
                    const glm::vec4& color,
                    float            thickness)
{
    if (glm::length(to - from) < 0.5f) {
        return;
    }
    list.strokeLine(from, to, color, thickness);
}

void drawScreenCircleOutline(ScreenDrawList& list,
                             const glm::vec2& center,
                             float            radius,
                             const glm::vec4& color,
                             float            thickness)
{
    constexpr int kSegments = 16;
    glm::vec2     prev      = center + glm::vec2(radius, 0.0f);
    for (int i = 1; i <= kSegments; ++i) {
        const float     angle = (2.0f * std::numbers::pi_v<float>) * static_cast<float>(i) /
                            static_cast<float>(kSegments);
        const glm::vec2 next = center + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
        drawScreenLine(list, prev, next, color, thickness);
        prev = next;
    }
}

void drawHatchedAxis(ScreenDrawList& list, const glm::vec2& origin,
                     const glm::vec2& end,
                     const glm::vec4& color)
{
    const glm::vec2 delta = end - origin;
    const float     len   = glm::length(delta);
    if (len < 8.0f) {
        return;
    }
    const glm::vec2 dir = delta / len;
    for (int dash = 1; dash < 10; ++dash) {
        const float startT = (static_cast<float>(dash * 2) * 0.05f) * len;
        const float endT   = (static_cast<float>(dash * 2 + 1) * 0.05f) * len;
        if (startT >= len) {
            break;
        }
        drawScreenLine(list, origin + dir * startT,
                       origin + dir * std::min(endT, len),
                       color,
                       1.5f);
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
                            const glm::vec3&                world,
                            glm::vec2&                      outScreen)
{
    const glm::vec4 clip = host.projection * host.view * glm::vec4(world, 1.0f);
    if (std::abs(clip.w) <= 1e-5f || clip.z <= -clip.w) {
        return false;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    outScreen.x         = (ndc.x * 0.5f + 0.5f) * host.extent.x;
    outScreen.y         = (1.0f - (ndc.y * 0.5f + 0.5f)) * host.extent.y;
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

std::optional<glm::vec3> intersectRayPlane(const Ray&       ray,
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
    glm::vec3       planeNormal = glm::cross(axis, tangent);
    if (glm::dot(planeNormal, planeNormal) <= 1e-5f) {
        planeNormal = choosePerpendicular(axis);
    }
    return glm::normalize(planeNormal);
}

float signedAngleAroundAxis(const glm::vec3& from, const glm::vec3& to, const glm::vec3& axis)
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

std::optional<FViewportGizmoFrame> buildViewportGizmoFrame(Entity*                         selectedEntity,
                                                           const FEditorViewportHostState& host,
                                                           EEditorViewportGizmoMode        mode)
{
    if (!selectedEntity || !selectedEntity->isValid() ||
        !selectedEntity->hasComponent<TransformComponent>()) {
        return std::nullopt;
    }

    // Generated companions follow their host, so a manipulator on one would
    // fight the reconciler that rebuilds it. The body stays selectable (the
    // hit resolves to the host), it just carries no transform handles.
    if (!CompanionManager::isAuthorEditable(*selectedEntity)) {
        return std::nullopt;
    }

    auto* transform = selectedEntity->getComponent<TransformComponent>();
    auto* id        = selectedEntity->getComponent<IDComponent>();
    if (!transform || !id) {
        return std::nullopt;
    }

    FViewportGizmoFrame frame;
    frame.entity          = selectedEntity;
    frame.entityUuid      = id->_id.value;
    const glm::mat4 world = transform->getTransform();
    if (!decomposeTrs(world, frame.originWorld, frame.rotation, frame.scale)) {
        return std::nullopt;
    }
    if (!projectWorldToViewport(host, frame.originWorld, frame.originScreen)) {
        return std::nullopt;
    }

    frame.cameraWorld     = glm::vec3(glm::inverse(host.view)[3]);
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
        const glm::vec3 plusWorld  = frame.originWorld + axisFrame.worldDir * frame.axisLengthWorld;
        const glm::vec3 minusWorld = frame.originWorld - axisFrame.worldDir * frame.axisLengthWorld;
        glm::vec2       plusScreen{0.0f};
        glm::vec2       minusScreen{0.0f};
        const bool      plusOk  = projectWorldToViewport(host, plusWorld, plusScreen);
        const bool      minusOk = projectWorldToViewport(host, minusWorld, minusScreen);
        const float     plusLen =
            plusOk ? glm::distance(frame.originScreen, plusScreen) : 0.0f;
        const float minusLen =
            minusOk ? glm::distance(frame.originScreen, minusScreen) : 0.0f;
        axisFrame.bReversed = minusOk && minusLen > plusLen + 1.0f;
        if (axisFrame.bReversed) {
            axisFrame.worldEnd   = minusWorld;
            axisFrame.screenEnd  = minusScreen;
            axisFrame.bProjected = true;
        }
        else {
            axisFrame.worldEnd   = plusWorld;
            axisFrame.screenEnd  = plusScreen;
            axisFrame.bProjected = plusOk;
        }
        frame.axes[i]        = axisFrame;
    }
    return frame;
}

EEditorViewportGizmoAxis hitTestLinearAxis(const FViewportGizmoFrame& frame, const glm::vec2& point)
{
    EEditorViewportGizmoAxis bestAxis   = EEditorViewportGizmoAxis::None;
    float                    bestDistSq = kViewportGizmoLineHitPixels * kViewportGizmoLineHitPixels;
    for (const auto& axis : frame.axes) {
        if (!axis.bProjected) {
            continue;
        }
        float       t      = 0.0f;
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
    const Ray                ray          = makeViewportRay(host, point);
    EEditorViewportGizmoAxis bestAxis     = EEditorViewportGizmoAxis::None;
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

} // namespace

void EditorViewportGizmoController::bind(App* app, FEditorViewportGizmoSources sources)
{
    _app     = app;
    _sources = std::move(sources);
}

Entity* EditorViewportGizmoController::selectedEntity() const
{
    return _sources.getSelectedEntity ? _sources.getSelectedEntity() : nullptr;
}

const std::vector<Entity*>& EditorViewportGizmoController::selections() const
{
    return _sources.getSelections ? _sources.getSelections() : kEmptyGizmoSelection;
}

Scene* EditorViewportGizmoController::viewportScene() const
{
    return _sources.getViewportInteractionScene ? _sources.getViewportInteractionScene() : nullptr;
}

bool EditorViewportGizmoController::hasSelectedEntities() const
{
    // Runtime-active reports no targets: the W/E/R switch and every pointer
    // pick below share this gate, so the authoring gizmo is fully out of the
    // frame while the game owns the viewport.
    return !_bRuntimeActive && !selections().empty();
}

bool EditorViewportGizmoController::consumeReleasePick()
{
    const bool consume     = _bConsumeReleasePick;
    _bConsumeReleasePick = false;
    return consume;
}

bool EditorViewportGizmoController::hasViewportGizmoSelection() const
{
    if (!_bHostValid || _bRuntimeActive) {
        return false;
    }
    Entity* entity = selectedEntity();
    return entity && entity->isValid() && entity->hasComponent<TransformComponent>() &&
           entity->getComponent<IDComponent>() != nullptr;
}

void EditorViewportGizmoController::syncHost(const FEditorViewportHostState& host)
{
    _hostState       = host;
    _bHostValid = host.extent.x > 0.0f && host.extent.y > 0.0f;
    if (!_bHostValid) {
        cancelDrag();
        _bHovered    = false;
        _hoveredAxis = EEditorViewportGizmoAxis::None;
        return;
    }
    if (!host.bHovered && !_bDragging) {
        _bPointerInside = false;
        _bHovered       = false;
        _hoveredAxis    = EEditorViewportGizmoAxis::None;
    }
}

void EditorViewportGizmoController::setPointer(const glm::vec2& localPoint, bool insideViewport)
{
    _pointerLocal   = localPoint;
    _bPointerInside = insideViewport;
    if (_bDragging) {
        updateDrag(localPoint);
        return;
    }
    if (!insideViewport || !hasViewportGizmoSelection()) {
        _bHovered    = false;
        _hoveredAxis = EEditorViewportGizmoAxis::None;
        return;
    }

    const auto frame = buildViewportGizmoFrame(selectedEntity(), _hostState, _mode);
    if (!frame.has_value()) {
        _bHovered    = false;
        _hoveredAxis = EEditorViewportGizmoAxis::None;
        return;
    }

    _hoveredAxis = _operation == EEditorViewportGizmoOperation::Rotate
                       ? hitTestRotateAxis(_hostState, *frame, localPoint)
                       : hitTestLinearAxis(*frame, localPoint);
    _bHovered = _hoveredAxis != EEditorViewportGizmoAxis::None;
}

bool EditorViewportGizmoController::beginDrag(const glm::vec2& localPoint)
{
    setPointer(localPoint, true);
    if (_bDragging || !hasViewportGizmoSelection()) {
        return false;
    }

    const auto frame = buildViewportGizmoFrame(selectedEntity(), _hostState, _mode);
    if (!frame.has_value()) {
        return false;
    }

    const EEditorViewportGizmoAxis axis =
        _operation == EEditorViewportGizmoOperation::Rotate
            ? hitTestRotateAxis(_hostState, *frame, localPoint)
            : hitTestLinearAxis(*frame, localPoint);
    if (axis == EEditorViewportGizmoAxis::None) {
        return false;
    }

    const FGizmoAxisFrame& axisFrame     = frame->axes[gizmoAxisIndex(axis)];
    const Ray              ray           = makeViewportRay(_hostState, localPoint);
    const glm::vec3        cameraToOrigin = glm::normalize(frame->originWorld - frame->cameraWorld);

    _activeAxis          = axis;
    _hoveredAxis         = axis;
    _bHovered            = true;
    _bDragging           = true;
    _bConsumeReleasePick = true;
    _dragStartPrimaryWorld = frame->entity->getComponent<TransformComponent>()->getTransform();
    _dragAxisWorld         = axisFrame.worldDir;
    _dragOriginWorld       = frame->originWorld;
    _undoBefore            = captureEditorTransformSelection(selections());

    if (_operation == EEditorViewportGizmoOperation::Rotate) {
        _dragPlaneNormal = axisFrame.worldDir;
        const auto hit   = intersectRayPlane(ray, frame->originWorld, _dragPlaneNormal);
        if (!hit.has_value()) {
            cancelDrag();
            return false;
        }
        const glm::vec3 fromOrigin = *hit - frame->originWorld;
        if (glm::dot(fromOrigin, fromOrigin) <= 1e-5f) {
            cancelDrag();
            return false;
        }
        _dragStartPlaneVector = glm::normalize(fromOrigin);
        _dragStartScalar      = 0.0f;
        return true;
    }

    _dragPlaneNormal = buildAxisDragPlaneNormal(axisFrame.worldDir, cameraToOrigin);
    const auto hit   = intersectRayPlane(ray, frame->originWorld, _dragPlaneNormal);
    if (!hit.has_value()) {
        cancelDrag();
        return false;
    }
    _dragStartScalar      = glm::dot(*hit - frame->originWorld, axisFrame.worldDir);
    _dragStartPlaneVector = glm::vec3(0.0f);
    return true;
}

void EditorViewportGizmoController::updateDrag(const glm::vec2& localPoint)
{
    if (!_bDragging || !_app) {
        return;
    }

    Entity* entity = selectedEntity();
    if (!entity || !entity->isValid() || !entity->hasComponent<TransformComponent>()) {
        cancelDrag();
        return;
    }

    glm::vec3 startPosition;
    glm::quat startRotation;
    glm::vec3 startScale;
    if (!decomposeTrs(_dragStartPrimaryWorld, startPosition, startRotation, startScale)) {
        cancelDrag();
        return;
    }

    const Ray ray             = makeViewportRay(_hostState, localPoint);
    glm::mat4 newPrimaryWorld = _dragStartPrimaryWorld;

    if (_operation == EEditorViewportGizmoOperation::Rotate) {
        const auto hit = intersectRayPlane(ray, _dragOriginWorld, _dragPlaneNormal);
        if (!hit.has_value()) {
            return;
        }
        glm::vec3 planeVector = *hit - _dragOriginWorld;
        if (glm::dot(planeVector, planeVector) <= 1e-5f) {
            return;
        }
        planeVector = glm::normalize(planeVector);
        float angle = signedAngleAroundAxis(_dragStartPlaneVector, planeVector, _dragPlaneNormal);
        if (isViewportGizmoSnapEnabled(*_app)) {
            angle = glm::radians(snapScalar(glm::degrees(angle), kViewportGizmoRotateSnapDeg));
        }
        newPrimaryWorld =
            composeTrs(startPosition, glm::angleAxis(angle, _dragPlaneNormal) * startRotation, startScale);
    }
    else {
        const auto hit = intersectRayPlane(ray, _dragOriginWorld, _dragPlaneNormal);
        if (!hit.has_value()) {
            return;
        }
        float delta = glm::dot(*hit - _dragOriginWorld, _dragAxisWorld) - _dragStartScalar;
        if (_operation == EEditorViewportGizmoOperation::Translate) {
            if (isViewportGizmoSnapEnabled(*_app)) {
                delta = snapScalar(delta, kViewportGizmoTranslateSnap);
            }
            glm::vec3 position = startPosition + _dragAxisWorld * delta;
            if (_sources.isEditorOrthoXY && _sources.isEditorOrthoXY()) {
                position.z = startPosition.z;
            }
            newPrimaryWorld = composeTrs(position, startRotation, startScale);
        }
        else {
            const float axisLengthWorld =
                std::max(computeWorldUnitsPerPixel(_hostState, _dragOriginWorld) * kViewportGizmoAxisPixels, 0.2f);
            float scaleDelta = delta / axisLengthWorld;
            if (isViewportGizmoSnapEnabled(*_app)) {
                scaleDelta = snapScalar(scaleDelta, kViewportGizmoScaleSnap);
            }
            glm::vec3    newScale  = startScale;
            const size_t axisIndex = gizmoAxisIndex(_activeAxis);
            newScale[axisIndex]    = clampScaleValue(startScale[axisIndex] + scaleDelta);
            newPrimaryWorld        = composeTrs(startPosition, startRotation, newScale);
        }
    }

    auto* primaryTransform = entity->getComponent<TransformComponent>();
    TransformSystem::setWorldTransform(primaryTransform, newPrimaryWorld);

    Scene* scene = viewportScene();
    if (scene && _undoBefore.size() > 1) {
        const glm::mat4 deltaWorld   = newPrimaryWorld * glm::inverse(_dragStartPrimaryWorld);
        const uint64_t  primaryUuid  = _undoBefore.empty() ? 0 : _undoBefore.front().entityUUID;
        for (const auto& snapshot : _undoBefore) {
            if (snapshot.entityUUID == primaryUuid) {
                continue;
            }
            Entity* other = scene->getEntityByUUID(snapshot.entityUUID);
            if (!other || !other->isValid() || !other->hasComponent<TransformComponent>()) {
                continue;
            }
            TransformSystem::setWorldTransform(other->getComponent<TransformComponent>(),
                                               deltaWorld * snapshot.world);
        }
    }
}

void EditorViewportGizmoController::endDrag()
{
    if (!_bDragging) {
        return;
    }
    Scene*                                    scene = viewportScene();
    std::vector<FEditorTransformSnapshot>     after = captureEditorTransformSelection(selections());
    if (_undo && scene && after.size() == _undoBefore.size()) {
        (void)pushEditorTransformUndo(*_undo, scene, _undoBefore, std::move(after));
    }
    _bDragging           = false;
    _activeAxis          = EEditorViewportGizmoAxis::None;
    _bConsumeReleasePick = true;
    _undoBefore.clear();
    setPointer(_pointerLocal, _bPointerInside);
    if (_sources.onTransformCommitted) {
        _sources.onTransformCommitted();
    }
}

void EditorViewportGizmoController::cancelDrag()
{
    _bDragging           = false;
    _bHovered            = false;
    _activeAxis          = EEditorViewportGizmoAxis::None;
    _hoveredAxis         = EEditorViewportGizmoAxis::None;
    _bConsumeReleasePick = false;
    _undoBefore.clear();
}

void EditorViewportGizmoController::setRuntimeActive(const bool bActive)
{
    if (_bRuntimeActive == bActive) {
        return;
    }
    _bRuntimeActive = bActive;
    if (bActive) {
        // Entering play with a control session: drop every hover/drag artefact
        // so nothing of the authoring gizmo survives into the game view.
        cancelDrag();
        _bHovered    = false;
        _hoveredAxis = EEditorViewportGizmoAxis::None;
    }
}

void EditorViewportGizmoController::setOperation(EEditorViewportGizmoOperation operation)
{
    _operation = operation;
    if (_bPointerInside && !_bDragging) {
        setPointer(_pointerLocal, true);
    }
}

void EditorViewportGizmoController::recordOverlay(ScreenDrawList& list) const
{
    YA_PROFILE_FUNCTION();
    if (!hasViewportGizmoSelection()) {
        return;
    }

    const auto frame = buildViewportGizmoFrame(selectedEntity(), _hostState, _mode);
    if (!frame.has_value()) {
        return;
    }

    const EEditorViewportGizmoAxis highlightedAxis = _bDragging ? _activeAxis : _hoveredAxis;

    if (_operation == EEditorViewportGizmoOperation::Rotate) {
        for (const auto& axis : frame->axes) {
            const bool      highlighted = axis.axis == highlightedAxis;
            const bool      pressed     = _bDragging && axis.axis == _activeAxis;
            const glm::vec4 color       = gizmoAxisColor(axis.axis, highlighted, pressed);
            const float     thickness   = gizmoLineThickness(highlighted, pressed);
            const glm::vec3 tangent0    = choosePerpendicular(axis.worldDir);
            const glm::vec3 tangent1    = glm::normalize(glm::cross(axis.worldDir, tangent0));
            glm::vec2       prevScreen{};
            bool            bPrev = false;
            for (int segment = 0; segment <= kViewportGizmoRingSegments; ++segment) {
                const float angle = (2.0f * std::numbers::pi_v<float>) * static_cast<float>(segment) /
                                    static_cast<float>(kViewportGizmoRingSegments);
                const glm::vec3 world =
                    frame->originWorld +
                    (tangent0 * std::cos(angle) + tangent1 * std::sin(angle)) * frame->ringRadiusWorld;
                glm::vec2 screen{};
                const bool bOk = projectWorldToViewport(_hostState, world, screen);
                if (bOk && bPrev) {
                    drawScreenLine(list, prevScreen, screen, color, thickness);
                }
                prevScreen = screen;
                bPrev      = bOk;
            }
        }
        return;
    }

    for (const auto& axis : frame->axes) {
        if (!axis.bProjected) {
            continue;
        }
        const bool      highlighted = axis.axis == highlightedAxis;
        const bool      pressed     = _bDragging && axis.axis == _activeAxis;
        const glm::vec4 color       = gizmoAxisColor(axis.axis, highlighted, pressed);
        const float     thickness   = gizmoLineThickness(highlighted, pressed);
        const float     handle      = gizmoHandlePixels(highlighted, pressed);
        drawScreenLine(list, frame->originScreen, axis.screenEnd, color, thickness);
        if (axis.bReversed) {
            drawHatchedAxis(list, frame->originScreen, axis.screenEnd, color);
            drawScreenCircleOutline(list, axis.screenEnd, handle * 0.55f, color, std::max(1.5f, thickness * 0.7f));
        }
        else {
            auto* white = TextureLibrary::get().getWhiteTexture().get();
            list.makeSprite(glm::vec3(axis.screenEnd.x - handle * 0.5f,
                                      axis.screenEnd.y - handle * 0.5f,
                                      0.0f),
                            glm::vec2(handle, handle),
                            white,
                            color);
        }
    }
}

} // namespace ya
