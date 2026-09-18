#include "GameRuntime/Lifecycle/FPSCtrl.h"

namespace ya
{

FPSControl *FPSControl::get()
{
    static FPSControl instance;
    return &instance;
}

} // namespace ya