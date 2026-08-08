#pragma once

#include "Core/Base.h"

#include <deque>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace ya
{

struct ICommandBuffer;
struct OffscreenJobState;

struct TaskManager
{
    std::deque<std::function<void()>> tasks;
    std::deque<std::pair<std::shared_ptr<OffscreenJobState>, std::function<void(ICommandBuffer*)>>> offscreenTasks;

    [[nodiscard]] bool hasFrameTasks() const { return !tasks.empty(); }
    [[nodiscard]] bool hasOffscreenTasks() const { return !offscreenTasks.empty(); }

    void registerFrameTask(std::function<void()> task)
    {
        tasks.push_back(std::move(task));
    }

    void enqueueOffscreenTask(const std::shared_ptr<OffscreenJobState>& job,
                              std::function<void(ICommandBuffer*)> task)
    {
        offscreenTasks.emplace_back(job, std::move(task));
    }

    void update()
    {
        while (!tasks.empty()) {
            auto task = std::move(tasks.front());
            tasks.pop_front();
            task();
        }
    }

    void updateOffscreenTasks(ICommandBuffer* cmdBuf,
                              std::vector<std::shared_ptr<OffscreenJobState>>* submittedJobs = nullptr)
    {
        while (!offscreenTasks.empty()) {
            auto [job, task] = std::move(offscreenTasks.front());
            offscreenTasks.pop_front();
            if (submittedJobs && job) {
                submittedJobs->push_back(job);
            }
            task(cmdBuf);
        }
    }
};

} // namespace ya
