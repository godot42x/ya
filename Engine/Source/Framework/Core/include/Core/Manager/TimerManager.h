#pragma once

#include <functional>
#include <map>
#include <vector>


namespace ya
{

struct TimerManager
{
    struct TimerData
    {
        std::function<void()> callback;
        float                 triggerTime; // Absolute time when timer should fire
        float                 interval;    // For repeating timers (0 = one-shot)
        bool                  repeating;
    };

    std::map<uint32_t, TimerData> _timers;
    std::vector<uint32_t>         _timersToRemove;
    uint32_t                      _nextTimerID = 1;
    float                         _currentTime = 0.0f;

    void onUpdate(float dt)
    {
        _currentTime += dt;

        // Execute timers that have reached their trigger time
        for (auto &[id, timer] : _timers)
        {
            if (_currentTime >= timer.triggerTime)
            {
                // Execute callback
                timer.callback();

                if (timer.repeating)
                {
                    // Reschedule repeating timer
                    timer.triggerTime = _currentTime + timer.interval;
                }
                else
                {
                    // Mark one-shot timer for removal
                    _timersToRemove.push_back(id);
                }
            }
        }

        // Remove completed one-shot timers
        for (uint32_t id : _timersToRemove)
        {
            _timers.erase(id);
        }
        _timersToRemove.clear();
    }

    /**
     * @brief Schedule a delayed one-shot callback
     * @param milliseconds Delay in milliseconds before callback is executed
     * @param callback Function to execute after delay
     * @return Timer ID that can be used to cancel the timer
     */
    uint32_t delayCall(uint32_t milliseconds, std::function<void()> callback)
    {
        float    delay = static_cast<float>(milliseconds) / 1000.0f;
        uint32_t id    = _nextTimerID++;

        _timers[id] = TimerData{
            .callback    = std::move(callback),
            .triggerTime = _currentTime + delay,
            .interval    = 0.0f,
            .repeating   = false,
        };

        return id;
    }

    /**
     * @brief Schedule a repeating timer
     * @param milliseconds Interval in milliseconds between executions
     * @param callback Function to execute at each interval
     * @return Timer ID that can be used to cancel the timer
     */
    uint32_t setInterval(uint32_t milliseconds, std::function<void()> callback)
    {
        float    interval = static_cast<float>(milliseconds) / 1000.0f;
        uint32_t id       = _nextTimerID++;

        _timers[id] = TimerData{
            .callback    = std::move(callback),
            .triggerTime = _currentTime + interval,
            .interval    = interval,
            .repeating   = true,
        };

        return id;
    }

    /**
     * @brief Cancel a scheduled timer
     * @param timerID ID returned from delayCall or setInterval
     * @return true if timer was found and canceled, false otherwise
     */
    bool cancelTimer(uint32_t timerID)
    {
        auto it = _timers.find(timerID);
        if (it != _timers.end())
        {
            _timers.erase(it);
            return true;
        }
        return false;
    }

    /**
     * @brief Clear all timers
     */
    void clearAllTimers()
    {
        _timers.clear();
        _timersToRemove.clear();
    }

    /**
     * @brief Get number of active timers
     */
    size_t getTimerCount() const
    {
        return _timers.size();
    }
};

} // namespace ya