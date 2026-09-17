#pragma once

// ============================================================================
// Platform export macro.
//
// This header only resolves the platform side of symbol visibility. Each
// module's export macro (YA_CORE_API / YA_RHI_API / ...) is injected by xmake
// as a preprocessor alias (e.g. `YA_CORE_API=YA_API_EXPORT`) in the module's
// own xmake.lua, so core stays unaware of the module list and no module table
// lives in a shared header.
//
//   YA_SHARED=1      the TU is compiled against shared libraries
//   YA_MODULE_BUILD  the TU is on the build (export) side of its module
// ============================================================================

#if defined(_WIN32)
    #if defined(YA_SHARED) && YA_SHARED
        #if defined(YA_MODULE_BUILD)
            #define YA_API_EXPORT __declspec(dllexport)
        #else
            #define YA_API_EXPORT __declspec(dllimport)
        #endif
    #else
        #define YA_API_EXPORT
    #endif
#else
    #define YA_API_EXPORT
#endif

// Historical aggregate alias, kept for the transition period.
#define ENGINE_API YA_API_EXPORT
