#include "UIManager.h"

namespace ya

{

UIManager *UIManager::get()
{
    static UIManager instance;
    return &instance;
}

UIElementRegistry *UIElementRegistry::get()
{
    static UIElementRegistry instance;
    return &instance;
}

} // namespace ya
