#include "PerfLab.h"

extern "C" YA_MODULE_EXPORT const ya::FYaModuleApi* yaGetModuleApi(uint32_t hostAbi)
{
    static const ya::FYaModuleApi api{
        .name          = "PerfLab",
        .kind          = ya::EModuleKind::Project,
        .createModule  = []() -> ya::IModule* { return new PerfLabModule(); },
        .destroyModule = [](ya::IModule* module) { delete module; },
    };
    return hostAbi == ya::YA_MODULE_ABI_VERSION ? &api : nullptr;
}
