#include "FName.h"
#include <mutex>

NameRegistry *NameRegistry::_instance = nullptr;

NameRegistry &NameRegistry::get()
{
    static NameRegistry instance;
    return instance;
}
