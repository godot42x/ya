#pragma once

#include "Core/Camera.h"
#include "Core/Input/InputManager.h"
#include "KeyCode.h"

#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Render/RenderDefines.h"

namespace ya
{

struct FreeCameraController
{
    float _moveSpeed     = 5.0f; // Units per second
    float _rotationSpeed = 0.5f; // Degrees per mouse unit

    // Movement keys (configurable)
    EKey::T _forwardKey = EKey::K_W;
    EKey::T _backKey    = EKey::K_S;
    EKey::T _leftKey    = EKey::K_A;
    EKey::T _rightKey   = EKey::K_D;
    EKey::T _upKey      = EKey::K_Q;
    EKey::T _downKey    = EKey::K_E;

    // Mouse button for rotation
    EMouse::T _rotateButton = EMouse::Right;

  public:
    void update(FreeCamera &camera, const InputManager &inputManager, float deltaTime);

  private:
    bool handleKeyboardInput(FreeCamera &camera, const InputManager &inputManager, float deltaTime);
    bool handleMouseRotation(FreeCamera &camera, const InputManager &inputManager);
};


// Controller for the ECS camera entity (TransformComponent + CameraComponent)
struct OrbitCameraController
{
    float    _mouseSensitivity = 0.1f;
    float    _zoomSensitivity  = 0.1f;
    EMouse::T _rotateButton     = EMouse::Right;

  public:
    void update(TransformComponent &tc, CameraComponent &cc, const InputManager &inputManager, const Extent2D &extent);
};

} // namespace ya
