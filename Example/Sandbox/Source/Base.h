
#pragma once


#if _WIN32
    #ifdef YA_BUILD_SHARED
        #define SANDBOX_API __declspec(dllexport)
    #else
        #define SANDBOX_API __declspec(dllimport)
    #endif
#else
    #define SANDBOX_API
#endif