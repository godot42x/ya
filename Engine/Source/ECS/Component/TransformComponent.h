
#pragma once
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"
#include "Math/GLM.h"


namespace ya
{
struct TransformComponent : public IComponent
{
    YA_REFLECT_BEGIN(TransformComponent)
    YA_REFLECT_FIELD(_position)
    YA_REFLECT_FIELD(_rotation)
    YA_REFLECT_FIELD(_scale)
    YA_REFLECT_FIELD(testIntArr)
    YA_REFLECT_FIELD(testMap)
    YA_REFLECT_FIELD(testVecMap)
    YA_REFLECT_END()

    glm::mat4 _cachedMatrix = glm::mat4(1.0f);
    glm::vec3 _position     = {0.0f, 0.0f, 0.0f};
    glm::vec3 _rotation     = {0.0f, 0.0f, 0.0f};
    glm::vec3 _scale        = {1.0f, 1.0f, 1.0f};

    std::vector<int>             testIntArr = {1, 2, 3, 4, 5};
    std::map<std::string, float> testMap    = {
        {"12a", 1.0f},
        {"12b", 2.0f},
        {"12c", 3.0f},
        {"12a3", 1.0f},
        {"12b3", 2.0f},
        {"12c3", 3.0f},
        {"12aa", 1.0f},
        {"12ab", 2.0f},
        {"12ac", 3.0f},
        {"12aa3", 1.0f},
        {"12ab3", 2.0f},
        {"12ac3", 3.0f},
        {"12aa", 1.0f},
        {"12ab", 2.0f},
        {"12ac", 3.0f},
        {"12aa3", 1.0f},
        {"12ab3", 2.0f},
        {"12ac3", 3.0f},
        {"12aaa", 1.0f},
        {"12aab", 2.0f},
        {"12aac", 3.0f},
        {"12aaa3", 1.0f},
        {"12aab3", 2.0f},
        {"12aac3", 3.0f},
    };
    std::map<int, glm::vec3> testVecMap = {
        {1, {1.0f, 0.0f, 0.0f}},
        {2, {0.0f, 1.0f, 0.0f}},
        {3, {0.0f, 0.0f, 1.0f}},
        {4, {0.0f, 0.0f, 1.0f}},
        {5, {0.0f, 0.0f, 1.0f}},
        {6, {0.0f, 0.0f, 1.0f}},
        {7, {0.0f, 0.0f, 1.0f}},
        {8, {0.0f, 0.0f, 1.0f}},
        {9, {0.0f, 0.0f, 1.0f}},
        {10, {0.0f, 0.0f, 1.0f}},
        {100, {0.0f, 0.0f, 1.0f}},
        {43, {0.0f, 0.0f, 1.0f}},
        {51, {0.0f, 0.0f, 1.0f}},
        {61, {0.0f, 0.0f, 1.0f}},
        {71, {0.0f, 0.0f, 1.0f}},
        {81, {0.0f, 0.0f, 1.0f}},
        {91, {0.0f, 0.0f, 1.0f}},
        {110, {0.0f, 0.0f, 1.0f}},
    };

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
};
} // namespace ya