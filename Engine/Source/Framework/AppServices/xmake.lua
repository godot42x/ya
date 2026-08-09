-- App services: narrow host-service contracts shared by framework modules
-- and implemented by the Host. Depends on Core/RHI only; no framework or
-- product types. Public API exposed through include/AppServices/.
target("ya-app-services")
    set_kind(ya_target_kind())
    ya_std_module("YA_APP_SERVICES_API")
    add_includedirs("./include", { public = true })
    add_files("*.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("*.h")
    add_deps("ya-foundation-core", "ya-rhi", { public = true })
