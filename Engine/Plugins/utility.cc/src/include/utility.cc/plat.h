#pragma once


#if _WIN32
    #ifdef BUILD_SHARED_UTILITY_CC
        #define UTILITY_CC_API __declspec(dllexport)
    #else
        #define UTILITY_CC_API __declspec(dllimport)
    #endif
#else
    #define UTILITY_CC_API
#endif