#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace ya
{

// Forward declaration
struct IRender;

/**
 * @brief GPU-safe deferred resource deletion queue.
 *
 * Problem:
 *   In Vulkan, destroying a VkImage / VkBuffer while a submitted command buffer
 *   still references it causes VK_ERROR_DEVICE_LOST.  With N frames in-flight,
 *   a resource freed on frame F might still be used by the GPU on frame F-1
 *   (or F-2 with triple-buffering).
 *
 * Solution:
 *   Instead of calling vkDestroyImage() immediately, push a destructor lambda
 *   into this queue together with the *current* frame index.  At the start of
 *   each frame (after the corresponding fence wait), flush all entries whose
 *   frame index is old enough that the GPU is guaranteed to have finished.
 *
 * Usage:
 *   1. Engine startup:
 *        DeferredDeletionQueue::get().init(renderer, framesInFlight);
 *
 *   2. Frame begin (after vkWaitForFences):
 *        DeferredDeletionQueue::get().flush(currentFrameIndex);
 *
 *   3. When a resource should be destroyed:
 *        DeferredDeletionQueue::get().enqueue(currentFrameIndex, [img, dev]() {
 *            vkDestroyImage(dev, img, nullptr);
 *            vkFreeMemory(dev, mem, nullptr);
 *        });
 *
 *   4. Engine shutdown:
 *        DeferredDeletionQueue::get().flushAll();   // after vkDeviceWaitIdle
 *
 * The queue is thread-safe: enqueue() may be called from any thread.
 */
class DeferredDeletionQueue
{
  public:
    static DeferredDeletionQueue& get();

    /**
     * @brief Initialize the queue.
     * @param framesInFlight  Number of frames that may be in-flight simultaneously
     *                        (e.g. VulkanRender::flightFrameSize).
     *                        Resources are kept alive for at least this many frames.
     */
    void init(uint32_t framesInFlight);

    /**
     * @brief Enqueue a destructor to be called later.
     *
     * The destructor will NOT run until `flush()` is called with a frame index
     * that is at least `_framesInFlight` frames ahead of `frameIndex`.
     *
     * @param frameIndex   The frame on which the resource was last potentially used.
     * @param destructor   Callable that releases the GPU resource (vkDestroy*, vkFree*, ...).
     */
    void enqueue(uint64_t frameIndex, std::function<void()> destructor);

    /**
     * @brief Convenience: enqueue a shared_ptr so its ref-count is held until safe.
     *
     * Useful for shared_ptr<Texture> / shared_ptr<IImage> – the shared_ptr is
     * moved into the queue and destroyed (potentially calling the real destructor)
     * only after the GPU has finished with it.
     *
     * @tparam T           Type held by the shared_ptr.
     * @param frameIndex   Current frame index.
     * @param resource     shared_ptr to the resource; will be std::move'd.
     */
    template <typename T>
    void enqueueResource(uint64_t frameIndex, std::shared_ptr<T> resource)
    {
        if (!resource) return;
        // Move the shared_ptr into the lambda — it will be destroyed when the lambda is.
        enqueue(frameIndex, [captured = std::move(resource)]() mutable {
            captured.reset(); // explicit reset; destructor runs here
        });
    }

    /**
     * @brief Convenience: retire a shared_ptr resource using the current frame index.
     *
     * Equivalent to enqueueResource(currentFrame(), resource) — avoids the
     * repetitive pattern of fetching the singleton + current frame at every call site.
     */
    template <typename T>
    void retireResource(std::shared_ptr<T> resource)
    {
        if (!resource) return;
        enqueueResource(_currentFrame, std::move(resource));
    }

    /**
     * @brief Flush (execute & remove) all entries that are safe to delete.
     *
     * Call this once per frame, **after** waiting on the frame fence, with
     * the frame index that just became safe.
     *
     * An entry enqueued at frame E is flushed when
     *   currentFrameIndex >= E + _framesInFlight + 1
     *
     * @param currentFrameIndex  The frame index of the frame we are about to begin.
     * @return Number of destructors executed.
     */
    size_t flush(uint64_t currentFrameIndex);

    /**
     * @brief Flush ALL remaining entries unconditionally.
     *        Call after vkDeviceWaitIdle() at shutdown.
     * @return Number of destructors executed.
     */
    size_t flushAll();

    /**
     * @brief Number of pending entries.
     */
    [[nodiscard]] size_t pendingCount() const;

    /**
     * @brief Whether the queue has been initialized.
     */
    [[nodiscard]] bool isInitialized() const { return _initialized; }

    /**
     * @brief Get current frame index tracked by this queue.
     *        Updated by flush() calls — always returns the last value passed to flush().
     */
    [[nodiscard]] uint64_t currentFrame() const { return _currentFrame; }

  private:
    DeferredDeletionQueue()  = default;
    ~DeferredDeletionQueue() = default;

    DeferredDeletionQueue(const DeferredDeletionQueue&)            = delete;
    DeferredDeletionQueue& operator=(const DeferredDeletionQueue&) = delete;

    struct Entry
    {
        uint64_t                frameIndex;  ///< Frame on which the resource was retired.
        std::function<void()>   destructor;  ///< Callable that frees the GPU resource.
    };

    std::vector<Entry>  _entries;
    mutable std::mutex  _mutex;
    uint32_t            _framesInFlight = 1;
    uint64_t            _currentFrame   = 0;
    bool                _initialized    = false;
};

} // namespace ya
