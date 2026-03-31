#include "DeferredDeletionQueue.h"

#include "Core/Log.h"

#include <algorithm>

namespace ya
{

DeferredDeletionQueue& DeferredDeletionQueue::get()
{
    static DeferredDeletionQueue instance;
    return instance;
}

void DeferredDeletionQueue::init(uint32_t framesInFlight)
{
    std::lock_guard lock(_mutex);
    _framesInFlight = (framesInFlight < 1) ? 1 : framesInFlight;
    _initialized    = true;
    YA_CORE_INFO("DeferredDeletionQueue: initialized (framesInFlight={})", _framesInFlight);
}

void DeferredDeletionQueue::enqueue(uint64_t frameIndex, std::function<void()> destructor)
{
    if (!destructor) return;

    std::lock_guard lock(_mutex);
    _entries.push_back(Entry{
        .frameIndex = frameIndex,
        .destructor = std::move(destructor),
    });
}

size_t DeferredDeletionQueue::flush(uint64_t currentFrameIndex)
{
    std::lock_guard lock(_mutex);

    _currentFrame = currentFrameIndex;

    if (_entries.empty()) return 0;

    // An entry is safe to delete when currentFrameIndex >= entry.frameIndex + _framesInFlight + 1
    // This guarantees that all command buffers that could reference the resource have completed.
    //
    // Example with flightFrameSize=2:
    //   - Resource retired at frame 5
    //   - Safe to delete at frame 5 + 2 + 1 = 8
    //   - At frame 8, fence wait guarantees frames 6 and 7 have completed

    uint64_t safeThreshold = (_framesInFlight + 1);

    size_t executed = 0;

    // Partition: entries to delete vs entries to keep
    auto it = std::stable_partition(_entries.begin(), _entries.end(),
                                    [currentFrameIndex, safeThreshold](const Entry& e) {
                                        // Keep entries that are NOT yet safe
                                        return currentFrameIndex < e.frameIndex + safeThreshold;
                                    });

    // Execute destructors for safe entries (after the partition point)
    for (auto jt = it; jt != _entries.end(); ++jt) {
        jt->destructor();
        ++executed;
    }

    _entries.erase(it, _entries.end());

    if (executed > 0) {
        YA_CORE_TRACE("DeferredDeletionQueue::flush: executed {} destructors (frame={}, pending={})",
                       executed, currentFrameIndex, _entries.size());
    }

    return executed;
}

size_t DeferredDeletionQueue::flushAll()
{
    std::lock_guard lock(_mutex);

    size_t count = _entries.size();
    for (auto& entry : _entries) {
        entry.destructor();
    }
    _entries.clear();

    if (count > 0) {
        YA_CORE_INFO("DeferredDeletionQueue::flushAll: executed {} destructors", count);
    }

    return count;
}

size_t DeferredDeletionQueue::pendingCount() const
{
    std::lock_guard lock(_mutex);
    return _entries.size();
}

} // namespace ya
