#pragma once

#include "Foundation/Core/Base.h"
#include "Framework/Game/Gameplay/ECS/ECSRegistry.h"


#include <nlohmann/json.hpp>

#include "Foundation/Core/UUID.h"


namespace ya
{

// Component reflection extension: every reflected IComponent type registered
// with ECSRegistry during static init. Must appear after Reflection.h (which
// defines the no-op default) and before any component type definition, which
// every component header guarantees by including this file.
#ifdef ___YA_REFLECT_EXTENSION
    #undef ___YA_REFLECT_EXTENSION
#endif
#define ___YA_REFLECT_EXTENSION(type_name) \
    ::ya::ECSRegistry::get().registerComponent<type_name>(#type_name);


struct Entity;
struct IComponent
{

    YA_REFLECT_BEGIN(IComponent)
    YA_REFLECT_END()

    Entity* _owner;
    virtual ~IComponent() = default;

    void                  setOwner(Entity* entity) { _owner = entity; }
    [[nodiscard]] Entity* getOwner() const { return _owner; }

    virtual void onPostSerialize() {}
    virtual void onEdit() {}

    // Return false when the component owns its complete serialized representation.
    virtual bool useReflectionSerialization() const { return true; }
    virtual void serializeCustom(nlohmann::json& out) const { (void)out; }
    virtual void deserializeCustom(const nlohmann::json& in) { (void)in; }
    virtual void cloneCustom(const IComponent& src) { (void)src; }
};

// Basic component for entity identification
struct IDComponent : public IComponent
{
    UUID _id = UUID(0);

    IDComponent()                   = default;
    IDComponent(const IDComponent&) = default;
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

    AudioSourceComponent()                            = default;
    AudioSourceComponent(const AudioSourceComponent&) = default;
    AudioSourceComponent(uint32_t clipId) : _audioClipID(clipId) {}
};

} // namespace ya
