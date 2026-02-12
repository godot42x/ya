#include "SceneBus.h"

namespace ya
{


SceneBus& SceneBus::get()
{
    static SceneBus instance;
    return instance;
}

} // namespace ya
