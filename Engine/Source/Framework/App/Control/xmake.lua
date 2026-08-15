-- App control plane: shared control protocols (automation run policy,
-- automation control server, scriptable GUI event driver, golden-image diff).
-- Windowless by contract; depends on Core only, never on GUI or app forms.
target("ya-app-control")
    set_kind(ya_target_kind())
    ya_std_module("YA_APP_CONTROL_API")
    add_includedirs("./include", { public = true })
    add_files("*.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("*.h")
    add_deps("ya-foundation-core", { public = true })
    add_packages("asio", "cxxopts", "glm", "nlohmann_json", { public = true })
