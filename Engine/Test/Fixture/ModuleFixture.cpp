#include "Core/Module/Module.h"

namespace
{

struct FFixtureState
{
    int sequence = 0;
    int created  = 0;
    int loaded   = 0;
    int queried  = 0;
    int started  = 0;
    int stopped  = 0;
    int unloaded = 0;
    int destroyed = 0;
};

FFixtureState state;

struct FixtureModule final : ya::IModule
{
    bool onLoad(ya::FModuleContext&) override
    {
        state.loaded = ++state.sequence;
        return true;
    }

    bool onStart(const ya::FEngineContext&) override
    {
        state.started = ++state.sequence;
        return true;
    }

    void onStop() override { state.stopped = ++state.sequence; }
    void onUnload() override { state.unloaded = ++state.sequence; }

    void* queryInterface(ya::FInterfaceId interfaceId) override
    {
        state.queried = ++state.sequence;
        return interfaceId == ya::makeInterfaceId("ya.test.fixture/1") ? this : nullptr;
    }
};

} // namespace

extern "C" YA_MODULE_EXPORT const FFixtureState* yaGetFixtureState()
{
    return &state;
}

extern "C" YA_MODULE_EXPORT const ya::FYaModuleApi* yaGetModuleApi(uint32_t hostAbi)
{
    static const ya::FYaModuleApi api{
        .name = "Fixture",
        .kind = ya::EModuleKind::Runtime,
        .createModule = []() -> ya::IModule* {
            state.created = ++state.sequence;
            return new FixtureModule();
        },
        .destroyModule = [](ya::IModule* module) {
            delete module;
            state.destroyed = ++state.sequence;
        },
    };
    return hostAbi == ya::YA_MODULE_ABI_VERSION ? &api : nullptr;
}
