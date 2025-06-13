#include "FName.h"
#include <mutex>

NameRegistry *NameRegistry::_instance = nullptr;

void NameRegistry::init()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        if (!_instance)
        {
            _instance = new NameRegistry();
        }
    });
}
