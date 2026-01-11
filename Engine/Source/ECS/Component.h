#pragma once

#include "Core/Base.h"


#include "Core/UUID.h"
#include "Math/GLM.h"


namespace ya
{


struct Entity;
struct IComponent
{
    Entity *_owner;

    void                  setOwner(Entity *entity) { _owner = entity; }
    [[nodiscard]] Entity *getOwner() const { return _owner; }
};

// Basic component for entity identification
struct IDComponent : public IComponent
{
    UUID _id = UUID(0);

    IDComponent()                    = default;
    IDComponent(const IDComponent &) = default;
    IDComponent(UUID id) : _id(id) {}
    IDComponent(uint64_t id) : _id(id) {}
};

struct TagComponent : public IComponent
{
    YA_REFLECT_BEGIN(TagComponent)
    YA_REFLECT_FIELD(_tag)
    YA_REFLECT_END()

    std::string _tag;

    TagComponent()                     = default;
    TagComponent(const TagComponent &) = default;
    TagComponent(std::string tag) : _tag(std::move(tag)) {}

  public:

    std::string getTag() const { return _tag; }
    void        setTag(const std::string &tag) { _tag = tag; }
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

} // namespace ya
