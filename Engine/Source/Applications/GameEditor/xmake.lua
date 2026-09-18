target("ya-game-editor")
    set_kind(ya_target_kind())
    ya_std_module("YA_GAME_EDITOR_API")
    add_includedirs("./include", { public = true })
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_files("**.cpp")
    if get_config("ya_linkage") == "monolith" then
        -- Loaded by the runtime host; engine symbols resolve from the host
        -- exe (single engine instance) instead of embedding static libs.
        add_deps("ya-engine", { links = false })
        add_shflags("-undefined", "dynamic_lookup", { force = true })
    else
        add_deps("ya-engine")
        add_links("ya-engine")
    end
    if is_plat("windows") then
        add_cxxflags("/bigobj")
    end
