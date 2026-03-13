#include "BillboardComponent.h"

namespace ya

{

bool BillboardComponent::resolve()
{
    if (image.isValid()) {
        image.invalidate();
    }
    if (image.isValid() && !image.isLoaded()) {
        if (!image.resolve()) {
            YA_CORE_WARN("Billboard texture resolve failed");
        }
    }
    return true;
}

} // namespace ya
