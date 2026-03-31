#include "TaskQueue.h"

#include "Core/Log.h"

namespace ya
{

TaskQueue& TaskQueue::get()
{
    static TaskQueue instance;
    return instance;
}

TaskQueue::~TaskQueue()
{
    stop();
}

void TaskQueue::start(uint32_t numThreads)
{
    if (_running) return;

    _stopping.store(false, std::memory_order_release);
    _running = true;

    for (uint32_t i = 0; i < numThreads; ++i) {
        _workers.emplace_back([this]() { workerLoop(); });
    }

    YA_CORE_INFO("TaskQueue: started with {} worker threads", numThreads);
}

void TaskQueue::stop()
{
    if (!_running) return;

    {
        std::lock_guard lock(_queueMutex);
        _stopping.store(true, std::memory_order_release);
    }
    _condition.notify_all();

    for (auto& worker : _workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    _workers.clear();
    _running = false;

    // Drain any remaining main-thread callbacks
    while (auto cb = _mainThreadCallbacks.tryPop()) {
        (*cb)();
    }

    YA_CORE_INFO("TaskQueue: stopped");
}

void TaskQueue::workerLoop()
{
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock lock(_queueMutex);
            _condition.wait(lock, [this]() {
                return _stopping.load(std::memory_order_acquire) || !_tasks.empty();
            });

            if (_stopping.load(std::memory_order_acquire) && _tasks.empty()) {
                return;
            }

            task = std::move(_tasks.front());
            _tasks.pop();
        }

        task();
    }
}

void TaskQueue::processMainThreadCallbacks(uint32_t maxCallbacks)
{
    // Lock-free pop from MPSC queue — no mutex, no contention.
    uint32_t processed = 0;

    while (auto callback = _mainThreadCallbacks.tryPop()) {
        (*callback)();
        ++processed;

        if (maxCallbacks > 0 && processed >= maxCallbacks) {
            break;
        }
    }
}

size_t TaskQueue::pendingCount() const
{
    std::lock_guard lock(_queueMutex);
    return _tasks.size();
}

} // namespace ya
