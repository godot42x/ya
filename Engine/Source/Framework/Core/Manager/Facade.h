#pragma once


#include "Core/Base.h"

#include "ClockManager.h"
#include "TimerManager.h"


namespace ya
{
struct FacadeMode
{
    TimerManager timerManager;
    ClockManager clockManager;
};

// Module-local singleton accessor. Routed through a function (not a plain
// extern data symbol): the module export macro propagates into every consuming
// DLL, so a dllexport data symbol would have to be defined in each consumer
// (LNK2001), whereas a function symbol resolves from the owning DLL.
YA_CORE_API FacadeMode& facade();


}; // namespace ya