#include "BillboardComponent.h"

namespace ya

{

bool BillboardComponent::resolve()
{
    if (!image.hasPath()) {
        bDirty = false;
        return true;
    }

    if (image.isReady()) {
        bDirty = false;
        return true;
    }

    const auto result = image.resolve();
    if (result == EAssetResolveResult::Ready) {
        bDirty = false;
        return true;
    }

    if (result == EAssetResolveResult::Pending) {
        bDirty = true;
        return false;
    }

    YA_CORE_WARN("Billboard texture resolve failed");
    bDirty = false;
    return false;
}

} // namespace ya
