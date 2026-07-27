#pragma once

#if defined(_WIN32) && defined(BUILD_SHARED_REFLECTS_CORE)
    #if defined(REFLECTS_CORE_BUILD)
        #define REFLECTS_CORE_API __declspec(dllexport)
    #else
        #define REFLECTS_CORE_API __declspec(dllimport)
    #endif
#else
    #define REFLECTS_CORE_API
#endif
