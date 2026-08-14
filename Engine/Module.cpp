// Aggregated DLL entry translation unit for ya-engine.
//
// This target is a shared facade that carries no engine TU of its own (every
// engine source lives in a module target). A shared library still needs at
// least one object file so the linker can generate the DLL entry point
// (_DllMainCRTStartup) and an exported symbol so it produces an import library.
// See ya-gui-framework (Framework/GUI/Module.cpp) for the same pattern.
#if defined(_WIN32) && defined(YA_SHARED)
    #define YA_ENGINE_ANCHOR __declspec(dllexport)
#else
    #define YA_ENGINE_ANCHOR
#endif

namespace ya
{

YA_ENGINE_ANCHOR void ya_engine_module_anchor()
{
}

} // namespace ya
