// ============================================================================
// Module anchor table.
//
// The aggregate (ya-engine) compiles no engine TU of its own, so the linker
// would otherwise pull nothing from the module archives and the shared
// library would export no engine symbols. Referencing one anchor function per
// module makes the linker pull each module's object(s); with unity builds
// (the default) every module is a single object, so one reference pulls the
// whole module.
// ============================================================================

extern "C" void* ya_anchor_core();
extern "C" void* ya_anchor_rhi();
extern "C" void* ya_anchor_rhi_backend();
extern "C" void* ya_anchor_render_graph();
extern "C" void* ya_anchor_gui_runtime();
extern "C" void* ya_anchor_scene_3d();
extern "C" void* ya_anchor_gameplay_ecs();
extern "C" void* ya_anchor_resource();
extern "C" void* ya_anchor_render_3d();
extern "C" void* ya_anchor_physics();
extern "C" void* ya_anchor_host();

namespace
{

void* const kModuleAnchors[] = {
    ya_anchor_core(),
    ya_anchor_rhi(),
    ya_anchor_rhi_backend(),
    ya_anchor_render_graph(),
    ya_anchor_gui_runtime(),
    ya_anchor_scene_3d(),
    ya_anchor_gameplay_ecs(),
    ya_anchor_resource(),
    ya_anchor_render_3d(),
    ya_anchor_physics(),
    ya_anchor_host(),
};

} // namespace
