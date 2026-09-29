#include "Render/Resources/TextureSlotBinding.h"

#include "RHI/Backend/TextureLibrary.h"
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
    return TextureLibrary::get().getSampler(slot.samplerConfig.filterMode, slot.samplerConfig.addressMode);
}

TextureBinding slotToTextureBinding(const TextureSlot& slot)
{
    TextureBinding tb;
    tb.texture = resolveSlotTexture(slot);
    tb.sampler = resolveSlotSampler(slot);
    return tb;
}

} // namespace ya
