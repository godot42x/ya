target("GreedySnake")
do
    set_kind("shared")
    add_files("Source/*.cpp")
    if get_config("ya_linkage") == "monolith" then
        -- Engine symbols resolve from the host exe at dlopen time (single
        -- engine instance); compile-only dependency, no engine static libs.
        add_deps("ya-engine", { links = false })
        add_deps("ya-game-runtime", { links = false })
        add_shflags("-undefined", "dynamic_lookup", { force = true })
    else
        add_deps("ya-engine")
        add_deps("ya-game-runtime")
    end
end
