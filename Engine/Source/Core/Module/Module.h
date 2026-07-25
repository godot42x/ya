#pragma once

#include <cstdint>
#include <string_view>

namespace ya
{

class App;
struct AppDesc;
struct Event;
struct ICommandBuffer;
struct Scene;
enum class AppState;

using FInterfaceId = uint64_t;

consteval FInterfaceId makeInterfaceId(std::string_view name)
{
    FInterfaceId hash = 14695981039346656037ull;
    for (const char ch : name) {
        hash ^= static_cast<uint8_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash == 0 ? 1 : hash;
}

#define YA_MODULE_ABI_VERSION_TEXT "1"
inline constexpr uint32_t YA_MODULE_ABI_VERSION = 1;

#if defined(__clang__)
    #define YA_MODULE_TOOLCHAIN "clang-" __clang_version__
#elif defined(_MSC_VER)
    #define YA_MODULE_STRINGIZE_IMPL(value) #value
    #define YA_MODULE_STRINGIZE(value) YA_MODULE_STRINGIZE_IMPL(value)
    #define YA_MODULE_TOOLCHAIN "msvc-" YA_MODULE_STRINGIZE(_MSC_VER)
#elif defined(__GNUC__)
    #define YA_MODULE_TOOLCHAIN "gcc-" __VERSION__
#else
    #define YA_MODULE_TOOLCHAIN "unknown-toolchain"
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
    #define YA_MODULE_ARCHITECTURE "arm64"
#elif defined(_M_X64) || defined(__x86_64__)
    #define YA_MODULE_ARCHITECTURE "x64"
#else
    #define YA_MODULE_ARCHITECTURE "unknown-arch"
#endif

#if defined(YA_BUILD_MODE_DEBUG)
    #define YA_MODULE_BUILD_MODE "debug"
#elif defined(YA_BUILD_MODE_RELEASEDBG)
    #define YA_MODULE_BUILD_MODE "releasedbg"
#elif defined(YA_BUILD_MODE_PROFILE)
    #define YA_MODULE_BUILD_MODE "profile"
#elif defined(YA_BUILD_MODE_RELEASE)
    #define YA_MODULE_BUILD_MODE "release"
#else
    #define YA_MODULE_BUILD_MODE "unknown"
#endif

inline constexpr FInterfaceId YA_MODULE_BUILD_FINGERPRINT =
    makeInterfaceId(YA_MODULE_ABI_VERSION_TEXT "|" YA_MODULE_TOOLCHAIN "|" YA_MODULE_ARCHITECTURE "|" YA_MODULE_BUILD_MODE);

enum class EModuleKind : uint8_t
{
    Runtime,
    Project,
    Editor,
};

struct FModuleContext
{
    virtual ~FModuleContext() = default;
    virtual void* queryInterface(std::string_view moduleName, FInterfaceId interfaceId) const = 0;
};

struct FEngineContext
{
    void* app = nullptr;
};

struct IModule
{
    virtual ~IModule() = default;

    virtual bool onLoad(FModuleContext& context) = 0;
    virtual bool onStart(const FEngineContext& context) = 0;
    virtual void onStop() = 0;
    virtual void onUnload() = 0;
    virtual void* queryInterface(FInterfaceId interfaceId)
    {
        (void)interfaceId;
        return nullptr;
    }

    virtual void onConfigure(App& app, AppDesc& desc)
    {
        (void)app;
        (void)desc;
    }
    virtual void onAttach(App& app) { (void)app; }
    virtual void onDetach(App& app) { (void)app; }
    virtual bool onBeforeAppStateChange(App& app, AppState previousState, AppState nextState)
    {
        (void)app;
        (void)previousState;
        (void)nextState;
        return true;
    }
    virtual void onAfterAppStateChange(App& app, AppState previousState, AppState currentState)
    {
        (void)app;
        (void)previousState;
        (void)currentState;
    }
    virtual void onSceneActivated(App& app, Scene* scene)
    {
        (void)app;
        (void)scene;
    }
    virtual void onSceneDestroyed(App& app, Scene* scene)
    {
        (void)app;
        (void)scene;
    }
    virtual bool onEvent(App& app, const Event& event)
    {
        (void)app;
        (void)event;
        return false;
    }
    virtual void onLogic(App& app, float dt)
    {
        (void)app;
        (void)dt;
    }
    virtual void onBeforeRender(App& app, float dt)
    {
        (void)app;
        (void)dt;
    }
    virtual void onBeforePresentation(App& app, ICommandBuffer& commandBuffer, float dt)
    {
        (void)app;
        (void)commandBuffer;
        (void)dt;
    }
    virtual void onPresentation(App& app, ICommandBuffer& commandBuffer, float dt)
    {
        (void)app;
        (void)commandBuffer;
        (void)dt;
    }
};

using FCreateModule  = IModule* (*)();
using FDestroyModule = void (*)(IModule*);

struct FYaModuleApi
{
    uint32_t       structSize       = sizeof(FYaModuleApi);
    uint32_t       abiVersion       = YA_MODULE_ABI_VERSION;
    const char*    toolchain        = YA_MODULE_TOOLCHAIN;
    const char*    architecture     = YA_MODULE_ARCHITECTURE;
    const char*    buildMode        = YA_MODULE_BUILD_MODE;
    FInterfaceId   buildFingerprint = YA_MODULE_BUILD_FINGERPRINT;
    const char*    name             = nullptr;
    EModuleKind    kind             = EModuleKind::Runtime;
    FCreateModule  createModule     = nullptr;
    FDestroyModule destroyModule    = nullptr;
};

using FGetModuleApi = const FYaModuleApi* (*)(uint32_t hostAbi);

} // namespace ya

#if defined(_WIN32)
    #define YA_MODULE_EXPORT __declspec(dllexport)
#else
    #define YA_MODULE_EXPORT __attribute__((visibility("default")))
#endif
