
#pragma once
#include "ECS/Component.h"

#include "Core/System/AutoRegister.h"

namespace ya
{
struct TransformComponent : public IComponent
{
    YA_ECS_COMPONENT(TransformComponent)

    glm::mat4 _cachedMatrix = glm::mat4(1.0f);
    glm::vec3 _position     = {0.0f, 0.0f, 0.0f};
    glm::vec3 _rotation     = {0.0f, 0.0f, 0.0f};
    glm::vec3 _scale        = {1.0f, 1.0f, 1.0f};

    bool bDirty = false;


  public:
    [[nodiscard]] const glm::vec3 &getPosition() const { return _position; }
    void                           setPosition(const glm::vec3 &translation)
    {
        _position = translation;
        bDirty    = true;
    }

    const glm::vec3 &getRotation() const { return _rotation; }
    void             setRotation(const glm::vec3 &rotation)
    {
        _rotation = rotation;
        bDirty    = true;
    }

    const glm::vec3 &getScale() const { return _scale; }
    void             setScale(const glm::vec3 &scale)
    {
        _scale = scale;
        bDirty = true;
    }

    void calcMatrix()
    {
        if (!bDirty) {
            return;
        }
        glm::mat4 rotation = glm::mat4_cast(glm::quat(_rotation));
        _cachedMatrix      = glm::translate(glm::mat4(1.0f), _position) *
                        rotation *
                        glm::scale(glm::mat4(1.0f), _scale);
        bDirty = false;
    }


    [[nodiscard]] const glm::mat4 &getTransform()
    {
        calcMatrix();
        return _cachedMatrix;
    }


    // ========================================================================
    // 反射注册 - 自动生成序列化
    // ========================================================================
    static void registerReflection()
    {
        Register<TransformComponent>("TransformComponent")
            .constructor<>()
            .property("position", &TransformComponent::_position)
            .property("rotation", &TransformComponent::_rotation)
            .property("scale", &TransformComponent::_scale);
    }
};
} // namespace ya