#pragma once

// ============================================================================
// Module export macros.
//
// One macro per engine module (YA_CORE_API / YA_RHI_API / ...). Modules never
// hand-write export logic: this header is the single source of truth and xmake
// stamps two defines per module target (see Source/xmake.lua):
//   YA_SHARED=1       this TU is part of the shared ya-engine aggregate
//   YA_MODULE_BUILD   this TU is on the build (export) side
// Consumers of ya-engine only receive YA_SHARED=1 (public), so every symbol
// resolves to dllimport on Windows and to nothing on other platforms.
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

// One line per module; identical resolution by design while the aggregate
// stays the single shared boundary (see .agent/plan/gui-framework-module-split).
#define YA_CORE_API         YA_API_EXPORT
#define YA_RHI_API          YA_API_EXPORT
#define YA_RHI_BACKEND_API  YA_API_EXPORT
#define YA_RENDER_GRAPH_API YA_API_EXPORT
#define YA_UI_API           YA_API_EXPORT
#define YA_UI_SCENE_API     YA_API_EXPORT
#define YA_SCENE_CORE_API   YA_API_EXPORT
#define YA_SCENE_3D_API     YA_API_EXPORT
#define YA_ECS_API          YA_API_EXPORT
#define YA_RESOURCE_API     YA_API_EXPORT
#define YA_RENDER_3D_API    YA_API_EXPORT
#define YA_PHYSICS_API      YA_API_EXPORT
#define YA_HOST_API         YA_API_EXPORT
#define YA_EDITOR_API       YA_API_EXPORT

// Historical aggregate alias, kept for the transition period.
#define ENGINE_API          YA_API_EXPORT
