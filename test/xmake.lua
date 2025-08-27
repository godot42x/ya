do -- grab all cpp file under test folder as a target
    local bDebug = false
    local files = os.files("./*.cpp")
    for _, file in ipairs(files) do
        file = file:gsub("/", ".")
        file = file:gsub("\\", ".")
        file = file:gsub(".cpp", "")
        local targetName = "test." .. file

        print("add test target:", targetName)
        target(targetName)
        do
            if bDebug then
                print("add test unit:", targetName)
            end
            set_group("test")
            set_kind("binary")
            add_files(file)
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
