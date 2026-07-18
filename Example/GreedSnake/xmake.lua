target("GreedySnake")
do
    set_kind("binary")
    add_files("Source/*.cpp")
    -- add_rules("SourceFiles")
    add_deps("ya-runtime")
end


-- YaModule("app", { type = "binary" })
