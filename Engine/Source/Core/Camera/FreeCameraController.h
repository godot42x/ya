#pragma once
#include "CameraController.h"
#include "Core/Camera/Camera.h"
#include "Core/Input/InputManager.h"
#include "Core/KeyCode.h"




namespace ya
{


struct FreeCameraController : public CameraController
{
    float _moveSpeed     = 5.0f;  // Units per second
    float _rotationSpeed = 45.0f; // Degrees per mouse unit

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
    bool handleMouseRotation(FreeCamera &camera, const InputManager &inputManager, float deltaTime);
};


} // namespace ya
