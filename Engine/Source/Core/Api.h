#pragma once

#if defined(_WIN32)
    #if defined(BUILD_SHARED_YA) && BUILD_SHARED_YA
        #if defined(BUILD_LIBRARY) && BUILD_LIBRARY
            #define ENGINE_API __declspec(dllexport)
        #else
            #define ENGINE_API __declspec(dllimport)
        #endif
    #else
        #define ENGINE_API
    #endif
#else
    #define ENGINE_API
#endif
