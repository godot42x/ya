#pragma once

#include <glm/glm.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <string>
#include <utility>

namespace ya
{

struct IComponent
{
};

// Basic component for entity identification
struct IDComponent : public IComponent
{
    uint32_t _id = 0;

    IDComponent()                    = default;
    IDComponent(const IDComponent &) = default;
    IDComponent(uint32_t id) : _id(id) {}
};

struct TagComponent : public IComponent
{
    std::string _tag;

    TagComponent()                     = default;
    TagComponent(const TagComponent &) = default;
    TagComponent(std::string tag) : _tag(std::move(tag)) {}
};



struct SpriteRendererComponent : public IComponent
{
    glm::vec4 _color{1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t  _textureID    = 0;
    float     _tilingFactor = 1.0f;

    SpriteRendererComponent()                                = default;
    SpriteRendererComponent(const SpriteRendererComponent &) = default;
    SpriteRendererComponent(const glm::vec4 &color) : _color(color) {}
};

struct CameraComponent : public IComponent
{
    bool _primary          = true; // TODO: think about moving to Scene
    bool _fixedAspectRatio = false;

    float _fov         = 45.0f;
    float _aspectRatio = 1.778f;
    float _nearClip    = 0.1f;
    float _farClip     = 1000.0f;

    CameraComponent()                        = default;
    CameraComponent(const CameraComponent &) = default;

    glm::mat4 getProjection() const
    {
        return glm::perspective(glm::radians(_fov), _aspectRatio, _nearClip, _farClip);
    }
};

// Mesh renderer component for 3D rendering
struct MeshRendererComponent : public IComponent
{
    uint32_t _meshID         = 0;
    uint32_t _materialID     = 0;
    bool     _castShadows    = true;
    bool     _receiveShadows = true;

    MeshRendererComponent()                              = default;
    MeshRendererComponent(const MeshRendererComponent &) = default;
    MeshRendererComponent(uint32_t meshId, uint32_t materialId)
        : _meshID(meshId), _materialID(materialId) {}
};

// Light component for lighting
struct LightComponent : public IComponent
{
    enum class Type
    {
        Directional,
        Point,
        Spot
    };

    Type      _type           = Type::Directional;
    glm::vec3 _color          = {1.0f, 1.0f, 1.0f};
    float     _intensity      = 1.0f;
    float     _range          = 10.0f; // For point and spot lights
    float     _innerConeAngle = 30.0f; // For spot lights
    float     _outerConeAngle = 45.0f; // For spot lights

    LightComponent()                       = default;
    LightComponent(const LightComponent &) = default;
    LightComponent(Type type, const glm::vec3 &color, float intensity)
        : _type(type), _color(color), _intensity(intensity) {}
};

// Physics body component (placeholder for future physics integration)
struct RigidBodyComponent : public IComponent
{
    enum class BodyType
    {
        Static,
        Kinematic,
        Dynamic
    };

    BodyType  _type            = BodyType::Dynamic;
    bool      _fixedRotation   = false;
    float     _mass            = 1.0f;
    float     _linearDrag      = 0.0f;
    float     _angularDrag     = 0.05f;
    glm::vec3 _velocity        = {0.0f, 0.0f, 0.0f};
    glm::vec3 _angularVelocity = {0.0f, 0.0f, 0.0f};

    RigidBodyComponent()                           = default;
    RigidBodyComponent(const RigidBodyComponent &) = default;
    RigidBodyComponent(BodyType type) : _type(type) {}
};

// Box collider component
struct BoxColliderComponent : public IComponent
{
    glm::vec3 _offset    = {0.0f, 0.0f, 0.0f};
    glm::vec3 _size      = {1.0f, 1.0f, 1.0f};
    bool      _isTrigger = false;

    BoxColliderComponent()                             = default;
    BoxColliderComponent(const BoxColliderComponent &) = default;
    BoxColliderComponent(const glm::vec3 &size) : _size(size) {}
};

// Audio source component
struct AudioSourceComponent : public IComponent
{
    uint32_t _audioClipID  = 0;
    bool     _playOnAwake  = false;
    bool     _loop         = false;
    float    _volume       = 1.0f;
    float    _pitch        = 1.0f;
    float    _spatialBlend = 0.0f; // 0 = 2D, 1 = 3D

    AudioSourceComponent()                             = default;
    AudioSourceComponent(const AudioSourceComponent &) = default;
    AudioSourceComponent(uint32_t clipId) : _audioClipID(clipId) {}
};

// Script component for custom behavior
struct ScriptComponent : public IComponent
{
    std::string _scriptName;
    // Additional script-related data would go here

    ScriptComponent()                        = default;
    ScriptComponent(const ScriptComponent &) = default;
    ScriptComponent(const std::string &scriptName) : _scriptName(scriptName) {}
};

} // namespace ya
