

#pragma once

#include "Core/App/App.h"

extern ya::App *createApp();

#if defined YA_ENTRY_POINT

int main()
{
    try {
        auto *app = createApp();
        app->init();
        app->run();
        app->quit();
    }
    catch (const std::exception &e) {
        NE_CORE_ERROR("Exception caught in main: {}", e.what());
        return -1;
    }
    catch (...) {
        NE_CORE_ERROR("Unknown exception caught in main");
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Give time for logs to flush
    NE_CORE_INFO("Application exited successfully");
    return 0;
}
#endif