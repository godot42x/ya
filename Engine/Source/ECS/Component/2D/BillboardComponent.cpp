#include "BillboardComponent.h"

namespace ya

{

bool BillboardComponent::resolve()
{
    if (image.hasPath()) {
        image.invalidate();
    }
    if (image.hasPath() && !image.isReady()) {
        if (!image.resolve()) {
            YA_CORE_WARN("Billboard texture resolve failed");
        }
    }
    return true;
}

} // namespace ya
