-- App runtime: reusable native app kernel pieces shared by GUI apps and the
-- engine host. This layer owns generic window/runtime mechanisms only; it
-- must not depend on product/editor/game semantics.
target("ya-app-runtime")
    set_kind(ya_target_kind())
    ya_std_module("YA_APP_RUNTIME_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-rhi", { public = true })

