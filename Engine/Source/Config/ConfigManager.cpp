#include "ConfigManager.h"

namespace ya

{

ConfigManager& ConfigManager::get()
{
    static ConfigManager instance;
    return instance;
}

} // namespace ya
