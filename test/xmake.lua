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
            set_group("test")
            set_kind("binary")
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
