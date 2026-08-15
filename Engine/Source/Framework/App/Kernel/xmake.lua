-- App kernel: the single windowless application loop shared by GUI apps,
-- the engine host, headless tools and dedicated servers. Owns only the event
-- pump, frame timing, delegate ticks and the shared exit/automation policy.
-- It must not depend on GUI, window or any app-form semantics.
target("ya-app-kernel")
    set_kind(ya_target_kind())
    ya_std_module("YA_APP_KERNEL_API")
    add_includedirs("./include", { public = true })
    add_files("*.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("*.h")
    add_deps("ya-app-control", "ya-foundation-core", { public = true })
