#include "FName.h"
#include <mutex>

namespace ya
{


NameRegistry &NameRegistry::get()
{
    static NameRegistry instance;
    return instance;
}

} // namespace ya