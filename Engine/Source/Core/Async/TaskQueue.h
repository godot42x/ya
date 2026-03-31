#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "LockFreeQueue.h"

namespace ya
{

/**
 * @brief Status of an async task.
 */
enum class ETaskStatus : uint8_t
{
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled,
};

/**
 * @brief A handle to a submitted async task.
 *
 * Wraps std::shared_future<T> with status tracking.
 * Copy-safe: multiple holders can observe the same task.
 */
template <typename T>
class TaskHandle
{
  public:
    TaskHandle() = default;

    explicit TaskHandle(std::shared_future<T> future, std::shared_ptr<std::atomic<ETaskStatus>> status)
        : _future(std::move(future)), _status(std::move(status))
    {
    }

    /// Current status of the task.
    [[nodiscard]] ETaskStatus getStatus() const
    {
        return _status ? _status->load(std::memory_order_acquire) : ETaskStatus::Failed;
    }

    /// True when the result is ready (Completed or Failed).
    [[nodiscard]] bool isReady() const
    {
        if (!_future.valid()) return false;
        return _future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    /// Blocking wait – returns the result or throws on failure.
    T get() { return _future.get(); }

    /// Non-blocking: if ready, writes result and returns true.
    bool tryGet(T& out)
    {
        if (!isReady()) return false;
        out = _future.get();
        return true;
    }

    [[nodiscard]] bool valid() const { return _future.valid(); }

  private:
    std::shared_future<T>                     _future;
    std::shared_ptr<std::atomic<ETaskStatus>> _status;
};

/**
 * @brief Lightweight thread-pool / task queue for async IO.
 *
 * Architecture:
 *   _tasks queue            : mutex + condvar (workers MUST sleep when idle —
 *                             lock-free can't do that without busy-spinning)
 *   _mainThreadCallbacks    : MPSCQueue (lock-free, multiple workers push,
 *                             main thread pops — zero contention on the hot path)
 *
 * - Fixed number of worker threads (default 2, enough for file IO).
 * - `submit()` enqueues work → returns TaskHandle.
 * - `submitWithCallback()` also enqueues a main-thread callback (lock-free).
 * - `processMainThreadCallbacks()` must be called from the main thread
 *   each frame to execute deferred GPU-upload or cache-store work.
 */
class TaskQueue
{
  public:
    static TaskQueue& get();

    /**
     * @brief Start worker threads. Safe to call multiple times (no-op if running).
     * @param numThreads Number of IO worker threads.
     */
    void start(uint32_t numThreads = 2);

    /**
     * @brief Signal workers to stop and join all threads.
     */
    void stop();

    /**
     * @brief Submit a callable to be executed on a worker thread.
     * @return TaskHandle<R> where R = return type of func.
     *
     * The task queue (_tasks) uses mutex + condvar because workers must sleep
     * when idle.  Lock-free queues require busy-spin polling which wastes CPU.
     */
    template <typename Func>
    auto submit(Func&& func) -> TaskHandle<std::invoke_result_t<Func>>
    {
        using R = std::invoke_result_t<Func>;

        auto status  = std::make_shared<std::atomic<ETaskStatus>>(ETaskStatus::Pending);
        auto promise = std::make_shared<std::promise<R>>();
        auto future  = promise->get_future().share();

        auto task = [func = std::forward<Func>(func), promise, status]() mutable {
            status->store(ETaskStatus::Running, std::memory_order_release);
            try {
                if constexpr (std::is_void_v<R>) {
                    func();
                    promise->set_value();
                }
                else {
                    promise->set_value(func());
                }
                status->store(ETaskStatus::Completed, std::memory_order_release);
            }
            catch (...) {
                promise->set_exception(std::current_exception());
                status->store(ETaskStatus::Failed, std::memory_order_release);
            }
        };

        {
            std::lock_guard lock(_queueMutex);
            _tasks.emplace(std::move(task));
        }
        _condition.notify_one();

        return TaskHandle<R>(std::move(future), std::move(status));
    }

    /**
     * @brief Submit work on a worker thread, then run a callback on the main thread
     *        once the work is complete.
     *
     * The callback is delivered via a lock-free MPSC queue — worker threads push
     * without any mutex, main thread pops without contention.
     *
     * @param work    Executed on a worker thread (file IO, decode, etc.)
     * @param onDone  Executed on the main thread via processMainThreadCallbacks()
     *                Receives the result of `work`.
     * @return TaskHandle for the worker-side result.
     */
    template <typename Func, typename Callback>
    auto submitWithCallback(Func&& work, Callback&& onDone)
        -> TaskHandle<std::invoke_result_t<Func>>
    {
        using R = std::invoke_result_t<Func>;

        auto status  = std::make_shared<std::atomic<ETaskStatus>>(ETaskStatus::Pending);
        auto promise = std::make_shared<std::promise<R>>();
        auto future  = promise->get_future().share();

        auto task = [this,
                     work   = std::forward<Func>(work),
                     onDone = std::forward<Callback>(onDone),
                     promise,
                     status]() mutable

        {
            status->store(ETaskStatus::Running, std::memory_order_release);
            try {
                if constexpr (std::is_void_v<R>) {
                    work();
                    promise->set_value();
                    // Lock-free push to main-thread callback queue
                    _mainThreadCallbacks.push([onDone = std::move(onDone)]() { onDone(); });
                }
                else {
                    R result = work();
                    promise->set_value(result);
                    // Lock-free push to main-thread callback queue
                    _mainThreadCallbacks.push(
                        [onDone = std::move(onDone), result = std::move(result)]() mutable {
                            onDone(std::move(result));
                        });
                }
                status->store(ETaskStatus::Completed, std::memory_order_release);
            }
            catch (...) {
                promise->set_exception(std::current_exception());
                status->store(ETaskStatus::Failed, std::memory_order_release);
            }
        };

        {
            std::lock_guard lock(_queueMutex);
            _tasks.emplace(std::move(task));
        }
        _condition.notify_one();

        return TaskHandle<R>(std::move(future), std::move(status));
    }

    /**
     * @brief Process completed callbacks on the main thread (lock-free pop).
     *        Call this once per frame from the main/render thread.
     * @param maxCallbacks  Max callbacks to process per call (0 = all).
     */
    void processMainThreadCallbacks(uint32_t maxCallbacks = 0);

    /**
     * @brief Number of pending tasks in the work queue.
     */
    [[nodiscard]] size_t pendingCount() const;

    /**
     * @brief Whether the queue is running.
     */
    [[nodiscard]] bool isRunning() const { return _running; }

  private:
    TaskQueue() = default;
    ~TaskQueue();

    TaskQueue(const TaskQueue&)            = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    void workerLoop();

    // ── Worker task queue (mutex + condvar — workers sleep when idle) ────
    std::vector<std::thread>          _workers;
    std::queue<std::function<void()>> _tasks;
    mutable std::mutex                _queueMutex;
    std::condition_variable           _condition;
    std::atomic<bool>                 _stopping{false};
    bool                              _running{false};

    // ── Main-thread callback queue (lock-free MPSC) ─────────────────────
    // Multiple workers push (lock-free), main thread pops (single consumer).
    // No mutex needed — zero contention.
    MPSCQueue<std::function<void()>> _mainThreadCallbacks;
};

} // namespace ya
