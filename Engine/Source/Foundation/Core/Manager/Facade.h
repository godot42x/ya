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

extern YA_CORE_API FacadeMode Facade;


}; // namespace ya