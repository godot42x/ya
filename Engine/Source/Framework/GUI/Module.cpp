// Aggregated DLL entry translation unit for ya-gui-framework.
//
// This target is a shared facade that carries no real sources of its own; its
// public deps re-export the whole GUI module closure. A shared library still
// needs at least one object file so the linker can generate the DLL entry
// point (_DllMainCRTStartup), and at least one exported symbol so link.exe
// produces an import library for consumers. See ya-engine (imgui_demo.cpp) for
// the same pattern. The anchor is exported with a raw dllexport because this
// aggregate target does not define YA_GUI_API (that macro belongs to the four
// GUI modules).
#if defined(_WIN32) && defined(YA_SHARED)
    #define YA_GUI_FRAMEWORK_ANCHOR __declspec(dllexport)
#else
    #define YA_GUI_FRAMEWORK_ANCHOR
#endif

namespace ya
{

YA_GUI_FRAMEWORK_ANCHOR void ya_gui_framework_module_anchor()
{
}

} // namespace ya
