#include "Core/Reflection/DeferredInitializer.h"

#include "Core/Reflection/CoreValueTypes.h"

namespace ya::reflection
{

DeferredInitializerQueue& DeferredInitializerQueue::instance()
{
    static DeferredInitializerQueue queue;
    return queue;
}

void DeferredInitializerQueue::executeAll()
{
    // Before the deferred class registrations, so YA_REFLECT_BEGIN_EXTERNAL
    // for glm::vec2/3/4 reuses this row (same type, same name).
    registerCoreValueTypes();

    for (auto& initFunc : _initializers) {
        initFunc();
    }
    _initializers.clear();
}

} // namespace ya::reflection
