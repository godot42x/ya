#pragma once

#include <functional>
#include <vector>

namespace ya::reflection
{

class DeferredInitializerQueue
{
  public:
    static DeferredInitializerQueue& instance();

    void add(std::function<void()> initFunc)
    {
        _initializers.push_back(std::move(initFunc));
    }
    void executeAll();

  private:
    std::vector<std::function<void()>> _initializers;
};

inline void deferStaticInit(std::function<void()> initFunc)
{
    DeferredInitializerQueue::instance().add(std::move(initFunc));
}

} // namespace ya::reflection
