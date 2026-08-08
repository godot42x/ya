#include "GUI/Runtime/Resource/TextureSlotBinding.h"

#include "GUI/Runtime/Resource/TextureLibrary.h"
#include "RHI/Core/Texture.h"

namespace ya
{

ya::Ptr<Texture> resolveSlotTexture(const TextureSlot& slot)
{
    if (slot.textureRef.isLoaded()) {
        return slot.textureRef.getShared();
    }

    if (!slot.textureRef.hasPath()) {
        return TextureLibrary::get().getWhiteTexture();
    }

    return nullptr;
}

ya::Ptr<Sampler> resolveSlotSampler(const TextureSlot& slot)
{
    (void)slot;
    // TODO: When custom sampler creation is implemented, check slot.samplerConfig here.
    // For now, always return default sampler (backward compatible).
    return TextureLibrary::get().getDefaultSampler();
}

TextureBinding slotToTextureBinding(const TextureSlot& slot)
{
    TextureBinding tb;
    tb.texture = resolveSlotTexture(slot);
    tb.sampler = resolveSlotSampler(slot);
    return tb;
}

} // namespace ya
