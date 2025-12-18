#include "UIBase.h"

namespace ya

{

UIMeta *UIMeta::get()
{
    static UIMeta instance;
    return &instance;
}

} // namespace ya
