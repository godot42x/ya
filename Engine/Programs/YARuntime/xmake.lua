target("ya-runtime")
do
    set_kind("binary")
    add_files("Source/*.cpp")
    add_deps("ya-engine")
    -- This executable *is* the game app form, so it names the runtime shell
    -- rather than inheriting it from the engine aggregate.
    add_deps("ya-game-runtime")
    if get_config("ya_linkage") == "monolith" and is_plat("macosx") then
        -- Export the engine symbol table so project/editor plugins loaded
        -- via dlopen resolve against the single in-exe engine instance.
        add_ldflags("-Wl,-export_dynamic", { force = true })
    end
end
