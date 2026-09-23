-- Scratch prototypes (bus / enum_name / type_size / graphics.h / LazyStatic):
-- author sandboxes for language and API experiments. They are programs with their
-- own main(), not tests, and bus.cpp blocks on std::cin, so they stay OUT of the
-- `test` group on purpose -- building/running them stays an explicit opt-in.
do -- grab all cpp file under test folder as a target
    local bDebug = false
    local files = os.files("./*.cpp")
    for _, filepath in ipairs(files) do
        local file = filepath:gsub("/", ".")
        file = file:gsub("\\", ".")
        file = file:gsub(".cpp", "")
        local targetName = "test." .. file

        target(targetName)
        do
            if bDebug then
                print("add test unit:", targetName)
            end
            set_kind("binary")
            add_deps("ya-engine")
            if filepath:find("EditorPropertyGraphTest", 1, true) then
                add_deps("ya-game-editor")
            end
            add_files(filepath)
            target_end()
        end
    end
end


task("test")
do
    set_menu {
        usage = "xmake test",
        options = {
            { nil, "rule", "v", "debug", "the rule to config build mode " }
        }
    }
    on_run(function()
        os.exec("xmake b -g test")
        os.exec("xmake r -g test")
    end)
end
