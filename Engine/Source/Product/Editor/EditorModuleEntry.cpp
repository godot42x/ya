#include "Product/Editor/EditorModule.h"

namespace ya
{

extern "C" YA_MODULE_EXPORT const FYaModuleApi* yaGetModuleApi(uint32_t hostAbi)
{
    static const FYaModuleApi api{
        .name          = "ya-editor",
        .kind          = EModuleKind::Editor,
        .createModule  = []() -> IModule* { return createEditorModule().release(); },
        .destroyModule = [](IModule* module) { delete module; },
    };
    return hostAbi == YA_MODULE_ABI_VERSION ? &api : nullptr;
}

} // namespace ya
