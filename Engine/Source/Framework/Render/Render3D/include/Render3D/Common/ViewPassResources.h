#pragma once

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"
#include "RHI/Core/Texture.h"

#include <array>
#include <cstdint>
#include <vector>

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

/// One instanced draw: a slice of the View's instance buffer plus the texture
/// table those instances sample. `textures[0]` is the white sentinel.
struct SpriteTextureBatch
{
    uint32_t                   firstInstance = 0;
    uint32_t                   instanceCount = 0;
    DescriptorSetHandle        set{};
    std::vector<TextureBinding> textures;
};

/// One View's scene-sprite pass. The instance buffer is a flight-scoped upload
/// slice; each batch's texture bindings are held here so the descriptor write
/// stays valid until the submission that recorded the draw has retired.
struct Sprite2DPassBindings
{
    UniformBufferPassBinding          frame;
    FrameUploadArena::Allocation      instances;
    std::vector<SpriteTextureBatch>   batches;
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
