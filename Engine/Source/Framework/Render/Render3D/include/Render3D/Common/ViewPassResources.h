#pragma once

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"

#include <array>
#include <cstdint>

namespace ya
{

class RenderSubmission;
struct IDescriptorSetLayout;
struct IRender;

/// Uniform-buffer DS + upload slice owned by one View pass.
struct UniformBufferPassBinding
{
    DescriptorSetHandle              set{};
    FrameUploadArena::Allocation     slice{};

    [[nodiscard]] bool isValid() const { return slice.valid(); }
};

struct CombinedImageSamplerPassBinding
{
    DescriptorSetHandle set{};
};

struct SSAOPassBindings
{
    CombinedImageSamplerPassBinding inputs;
};

struct DeferredLightingPassBindings
{
    CombinedImageSamplerPassBinding gBufferTextures;
    CombinedImageSamplerPassBinding shadows;
};

struct EntityIdPassBindings
{
    UniformBufferPassBinding frame;
};

struct OverlayPassBindings
{
    UniformBufferPassBinding        billboardFrame;
    CombinedImageSamplerPassBinding billboardTextures;
};

/// One View's scene-sprite pass: its frame constant plus the sprite texture
/// table the View's candidates were deduped into.
struct Sprite2DPassBindings
{
    UniformBufferPassBinding        frame;
    CombinedImageSamplerPassBinding textures;
};

struct ForwardDebugPassBindings
{
    UniformBufferPassBinding ubo;
};

inline constexpr uint32_t kMaxBloomBlurPasses = 16;

struct BloomPassBindings
{
    CombinedImageSamplerPassBinding                              extract;
    std::array<CombinedImageSamplerPassBinding, kMaxBloomBlurPasses> blur{};
    CombinedImageSamplerPassBinding                              composite;
};

struct ToneMapPassBindings
{
    CombinedImageSamplerPassBinding input;
};

struct PostprocessPassBindings
{
    BloomPassBindings  bloom;
    ToneMapPassBindings toneMap;
};

bool writeUniformPassBinding(
    RenderSubmission&                   submission,
    IRender*                            render,
    const stdptr<IDescriptorSetLayout>& layout,
    uint32_t                            alignment,
    const void*                         data,
    uint32_t                            size,
    UniformBufferPassBinding&           out);

[[nodiscard]] DescriptorSetHandle allocateCombinedImageSamplerSet(
    RenderSubmission&                   submission,
    const stdptr<IDescriptorSetLayout>& layout,
    uint32_t                            descriptorsPerSet);

void allocateBloomPassBindings(
    RenderSubmission&                   submission,
    const stdptr<IDescriptorSetLayout>& extractLayout,
    const stdptr<IDescriptorSetLayout>& blurLayout,
    const stdptr<IDescriptorSetLayout>& compositeLayout,
    BloomPassBindings&                  out);

} // namespace ya
