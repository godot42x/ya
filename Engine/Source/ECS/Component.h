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
