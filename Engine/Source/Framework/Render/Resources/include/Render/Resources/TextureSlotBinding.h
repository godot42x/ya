#pragma once

// ============================================================================
// TextureSlot -> GPU binding helpers.
//
// TextureSlot (Core) is a pure authoring descriptor; converting it into
// resolved GPU bindings needs the GUI texture library (white/checkerboard
// fallbacks and the default sampler). These helpers are provided by the GUI
// resource layer so gameplay material components and render consumers do not
// carry GPU/gui types in their own headers.
// ============================================================================

#include "Core/Base.h"
#include "Core/Common/TextureSlot.h"

namespace ya
{

struct Sampler;
struct Texture;
struct TextureBinding;

/// Resolved texture for the slot (white-texture fallback when the slot is
/// empty, nullptr while a path is still loading).
YA_RENDER_RESOURCES_API ya::Ptr<Texture> resolveSlotTexture(const TextureSlot& slot);

/// Resolved sampler for the slot (default sampler for now).
YA_RENDER_RESOURCES_API ya::Ptr<Sampler> resolveSlotSampler(const TextureSlot& slot);

/// Build a TextureBinding from the slot's resolved resources.
YA_RENDER_RESOURCES_API TextureBinding slotToTextureBinding(const TextureSlot& slot);

/// Binding for a slot whose texture is still loading: the semantic default
/// (white, or a flat normal for normal-map slots), so a half-loaded material
/// shades correctly instead of sampling garbage.
YA_RENDER_RESOURCES_API TextureBinding loadingSlotFallback(bool bNormalMap);

/// Binding for a slot whose texture failed to load: an explicit
/// checkerboard, so the failure is visible where the texture would be.
YA_RENDER_RESOURCES_API TextureBinding failedSlotFallback();

} // namespace ya
