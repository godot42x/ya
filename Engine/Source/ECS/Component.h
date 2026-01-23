#pragma once

#include "Core/Base.h"


#include "Core/UUID.h"


namespace ya
{


struct Entity;
struct IComponent
{
    Entity *_owner;

    void                  setOwner(Entity *entity) { _owner = entity; }
    [[nodiscard]] Entity *getOwner() const { return _owner; }

    virtual void onPostSerialize() {}
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
