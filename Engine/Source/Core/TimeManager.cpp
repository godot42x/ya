#include "TimeManager.h"

namespace ya
{

TimeManager *TimeManager::get()
{
    static TimeManager instance;
    return &instance;
}

} // namespace ya
