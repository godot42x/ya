// #define SDL_MAIN_USE_CALLBACKS
// #include <SDL3/SDL_main.h>

#include "Core/App/App.h"


#pragma region Entry

int main(int argc, char **argv)
{
    try {

        ya::AppCreateInfo ci;
        ci.init(argc, argv);
        auto *app = new ya::App(ci);
        app->init();
        app->run();
        app->quit();
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
#pragma endregion