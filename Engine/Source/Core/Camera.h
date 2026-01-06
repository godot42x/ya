#pragma once

#include "Log.h"

#include "Math/Math.h"


namespace ya
{

struct Camera
{
    glm::mat4 projectionMatrix     = {};
    glm::mat4 viewMatrix           = {};
    glm::mat4 viewProjectionMatrix = {};

    Camera()                              = default;
    Camera(const Camera &)                = default;
    Camera &operator=(const Camera &)     = default;
    Camera(Camera &&) noexcept            = default;
    Camera &operator=(Camera &&) noexcept = default;

    [[nodiscard]] const glm::mat4 &getViewProjectionMatrix() const { return viewProjectionMatrix; }

    glm::mat4 getViewMatrix() const { return viewMatrix; }
    glm::mat4 getProjectionMatrix() const { return projectionMatrix; }

    virtual ~Camera() = default;
};



struct FreeCamera : public Camera
{
    float _fov         = 45.0f;
    float _aspectRatio = 1.6f / 0.9f;
    float _nearClip    = 0.1f;
    float _farClip     = 100.0f;

    glm::vec3 _position = {0.0f, 0.0f, 0.0f}; // Start a bit back from the origin
    glm::vec3 _rotation = {0.0f, 0.0f, 0.0f}; // pitch, yaw, roll

    enum EProjectionType
    {
        Perspective,
        Orthographic
    } projectionType = Perspective;

  public:
    FreeCamera() : Camera() {}
    FreeCamera(const FreeCamera &)                = default;
    FreeCamera &operator=(const FreeCamera &)     = default;
    FreeCamera(FreeCamera &&) noexcept            = default;
    FreeCamera &operator=(FreeCamera &&) noexcept = default;
    ~FreeCamera() override                        = default;

    void setPerspective(float fov, float aspectRatio, float nearClip, float farClip)
    {
        this->projectionType = EProjectionType::Perspective;
        this->_fov           = fov;
        if (fov < 1.f) {
            YA_CORE_WARN("FOV is too small {}", fov);
        }
        this->_aspectRatio = aspectRatio;
        this->_nearClip    = nearClip;
        this->_farClip     = farClip;

        // 使用 Vulkan 兼容的投影矩阵（右手坐标系，Z: [0,1]）
        // Y 轴翻转由渲染后端处理（VulkanRenderTarget 中 proj[1][1] *= -1）

        // 如果不处理，会出现前后上下方向的光照完全相反，eg: 灯光在cube的正上方/正右方， 但是下方/正左方反而产生了高亮
        // 这是反物理与反直觉的!
        // 使用了 ZO(zero to one) 兼容 vulkan 后该现象消失了
        projectionMatrix = glm::perspectiveRH_ZO(glm::radians(fov), aspectRatio, nearClip, farClip);

        // projectionMatrix = glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip);

        recalculateViewProjectionMatrix();
    }

    void setOrthographic(float left, float right, float bottom, float top, float nearClip, float farClip)
    {
        this->projectionType = EProjectionType::Orthographic;
        this->_nearClip      = nearClip;
        this->_farClip       = farClip;
        projectionMatrix     = glm::ortho(left, right, bottom, top, nearClip, farClip);

        recalculateViewProjectionMatrix();
    }



    void recalculateViewMatrix()
    {
        const glm::quat rotQuat = glm::quat(glm::radians(_rotation));

        // viewMatrix = glm::translate(glm::mat4(1.0f), _position) *
        //              glm::mat4_cast(rotQuat);

        // viewMatrix = glm::inverse(viewMatrix);
        viewMatrix = FMath::lookAt(_position,
                                   _position + (rotQuat * glm::vec3(0.0f, 0.0f, -1.0f)),
                                   rotQuat * glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void recalculateViewProjectionMatrix()
    {
        viewProjectionMatrix = projectionMatrix * viewMatrix;
    }

    void recalculateAll()
    {
        recalculateViewMatrix();
        recalculateViewProjectionMatrix();
    }

    const glm::vec3 &getPosition() const { return _position; }
    void             setPosition(const glm::vec3 &position_)
    {
        _position = position_;
        recalculateAll();
    }

    void setRotation(const glm::vec3 &rotation_)
    {
        _rotation = rotation_;
        recalculateAll();
    }

    void setPositionAndRotation(const glm::vec3 &position_, const glm::vec3 &rotation_)
    {
        _position = position_;
        _rotation = rotation_;
        recalculateAll();
    }

    void setAspectRatio(float aspectRatio)
    {
        this->_aspectRatio = aspectRatio;
        if (projectionType == EProjectionType::Perspective) {
            // projectionMatrix = glm::perspectiveRH_ZO(glm::radians(_fov), aspectRatio, _nearClip, _farClip);
            projectionMatrix = FMath::perspective(glm::radians(_fov), aspectRatio, _nearClip, _farClip);
        }
        else {
            // projectionMatrix = glm::orthoRH_ZO(-aspectRatio, aspectRatio, -1.0f, 1.0f, _nearClip, _farClip);
            projectionMatrix = FMath::orthographic(-aspectRatio, aspectRatio, -1.0f, 1.0f, _nearClip, _farClip);
        }
        recalculateViewProjectionMatrix();
    }
};


struct OrbitCamera : public Camera
{
    glm::vec3 _focalPoint = {0.0f, 0.0f, 0.0f};
    float     _distance   = 10.0f;

    glm::vec3 _position;
    glm::vec3 _rotation; // pitch, yaw, roll

  public:
    void setDistance(float distance)
    {
        this->_distance = distance;
        recalculateViewMatrix();
        recalculateViewProjectionMatrix();
    }

    void setFocalPoint(const glm::vec3 &focalPoint)
    {
        this->_focalPoint = focalPoint;
        recalculateViewMatrix();
        recalculateViewProjectionMatrix();
    }

    void recalculateViewMatrix()
    {
    }

    void recalculateViewProjectionMatrix()
    {
        viewProjectionMatrix = projectionMatrix * viewMatrix;
    }
};
} // namespace ya