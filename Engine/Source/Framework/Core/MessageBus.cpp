#include "MessageBus.h"

namespace ya

{

MessageBus *MessageBus::get()
{
    static MessageBus Instance;
    return &Instance;
}

} // namespace ya
