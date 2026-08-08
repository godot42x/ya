#pragma once

#include "Foundation/Core/Api.h"

#include <functional>
#include <vector>

namespace ya::reflection
{

class DeferredInitializerQueue
{
  public:
    static YA_CORE_API DeferredInitializerQueue& instance();

    void add(std::function<void()> initFunc)
    {
        _initializers.push_back(std::move(initFunc));
    }
    YA_CORE_API void executeAll();

  private:
    std::vector<std::function<void()>> _initializers;
};

inline void deferStaticInit(std::function<void()> initFunc)
{
    DeferredInitializerQueue::instance().add(std::move(initFunc));
}

} // namespace ya::reflection
