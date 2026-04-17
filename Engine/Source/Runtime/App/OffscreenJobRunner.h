#pragma once

#include <memory>

namespace ya
{

struct App;
struct IRender;
struct OffscreenJobState;

void queueOffscreenJob(App* app, IRender* render, const std::shared_ptr<OffscreenJobState>& job);
void cancelOffscreenJob(std::shared_ptr<OffscreenJobState>& job);

} // namespace ya