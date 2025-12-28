#pragma once


#include "Core/Base.h"
#include "TimerManager.h"


namespace ya
{
struct FacadeMode
{
    TimerManager timerManager;
};

extern ENGINE_API FacadeMode Facade;


}; // namespace ya