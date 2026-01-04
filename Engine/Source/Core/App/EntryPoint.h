

#pragma once

#include "Core/Profiling/StaticInitProfiler.h"

#include "Core/App/App.h"

extern ya::App *createApp();

#if defined YA_ENTRY_POINT

int main(int argc, char **argv)
{

    // 静态初始化已完成，显示性能报告（如果未自动打印）
    ya::profiling::StaticInitProfiler::refOBJ();
    ya::profiling::StaticInitProfiler::recordEnd();


    try {
        ya::AppDesc ci;
        ci.init(argc, argv);
        auto *app = createApp();
        app->init(ci);
        app->run();
        app->quit();
        delete app;
    }
    catch (const std::exception &e) {
        YA_CORE_ERROR("Exception caught in main: {}", e.what());
        return -1;
    }
    catch (...) {
        YA_CORE_ERROR("Unknown exception caught in main");
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Give time for logs to flush
    YA_CORE_INFO("Application exited successfully");
    return 0;
}
#endif