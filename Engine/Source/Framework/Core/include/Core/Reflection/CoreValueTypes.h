#pragma once

#include "Core/Api.h"

namespace ya::reflection
{

// Idempotent. Called at the start of DeferredInitializerQueue::executeAll so
// every process that flushes reflection also registers these value types.
// Tests may call it directly.
YA_CORE_API void registerCoreValueTypes();

} // namespace ya::reflection
