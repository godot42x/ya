#pragma once

#include "Foundation/Core/Api.h"

#include <memory>

namespace ya
{

struct Texture;
struct Sampler;

/// Default texture/sampler source used by render fallbacks (e.g. an unset
/// TextureBinding resolves to a 1x1 white texture). RHI/backend never reach
/// into higher layers for this: the GUI texture library (or any host layer)
/// implements the interface and registers itself at init time.
struct IBuiltinTextureSource
{
    virtual ~IBuiltinTextureSource() = default;

    virtual std::shared_ptr<Texture> getWhiteTexture()    = 0;
    virtual std::shared_ptr<Sampler> getDefaultSampler()  = 0;

    /// Release GPU resources while the render device is still alive (the
    /// source's static destructor may otherwise run after device teardown).
    virtual void shutdown() {}
};

/// Installed default-resource source (null in bare-RHI hosts). Defined in
/// BuiltinTextureSource.cpp so the registry slot has ONE strong symbol across
/// all dylibs (header-inline statics would be copied per image).
YA_RHI_API void                   setBuiltinTextureSource(IBuiltinTextureSource* source);
YA_RHI_API IBuiltinTextureSource* getBuiltinTextureSource();

} // namespace ya
