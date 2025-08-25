
#pragma once
#include "ECS/Component.h"

namespace ya
{
struct TransformComponent : public IComponent
{
    glm::vec3 _translation  = {0.0f, 0.0f, 0.0f};
    glm::vec3 _rotation     = {0.0f, 0.0f, 0.0f};
    glm::vec3 _scale        = {1.0f, 1.0f, 1.0f};
    glm::mat4 _cachedMatrix = glm::mat4(1.0f);

    void setPosition(const glm::vec3 &translation)
    {
        _translation = translation;
        calcMatrix();
    }
    void setRotation(const glm::vec3 &rotation)
    {
        _rotation = rotation;
        calcMatrix();
    }
    void setScale(const glm::vec3 &scale)
    {
        _scale = scale;
        calcMatrix();
    }

    void calcMatrix()
    {
        glm::mat4 rotation = glm::mat4_cast(glm::quat(_rotation));
        _cachedMatrix      = glm::translate(glm::mat4(1.0f), _translation) *
                        rotation *
                        glm::scale(glm::mat4(1.0f), _scale);
    }


    [[nodiscard]] glm::mat4 getTransform() const
    {
        return _cachedMatrix;
    }
};
} // namespace ya