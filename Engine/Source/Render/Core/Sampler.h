
#pragma once
#include "Render/RenderDefines.h"
namespace ya
{


struct Sampler
{
    void       *_impl = nullptr;
    SamplerDesc _desc;

    static stdptr<Sampler> create(const SamplerDesc &desc);

    template <typename T>
    T *as() { return static_cast<T *>(this); }
};
} // namespace ya