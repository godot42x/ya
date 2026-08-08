#include "DeferredInitializer.h"

namespace ya::reflection
{

DeferredInitializerQueue& DeferredInitializerQueue::instance()
{
    static DeferredInitializerQueue queue;
    return queue;
}

void DeferredInitializerQueue::executeAll()
{
    for (auto& initFunc : _initializers) {
        initFunc();
    }
    _initializers.clear();
}

} // namespace ya::reflection
