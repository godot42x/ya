#pragma once

#include <memory>

namespace ya
{

struct BufferCreateInfo;
struct IBuffer;
struct Sampler;
struct SamplerDesc;

struct IRenderResourceFactory
{
    virtual ~IRenderResourceFactory() = default;

    virtual std::shared_ptr<IBuffer> createBuffer(const BufferCreateInfo& desc) = 0;
    virtual std::shared_ptr<Sampler> createSampler(const SamplerDesc& desc) = 0;
};

} // namespace ya
