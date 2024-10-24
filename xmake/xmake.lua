task("test")
do
    set_menu {}
    on_run(function()
        for targetname, target in pairs(project.targets()) do
            print(targetname)
            print(target)
            print(target:targetfile())
            print(target:basename())
            print(target:filename())
            print(target:linkname())
            print(target:targetdir())
            print(target:kind())
            -- return

            -- local target_name = arg[1] or "__DEFAULT_VAR__"
            -- local target_dir = arg[2] or "__DEFAULT_VAR__"
            -- local target_base_name = arg[3] or "__DEFAULT_VAr__"
        end

        -- exec_cmds(
        --     "xmake f -m debug --test=y",
        --     -- "xmake f -m debug",
        --     "xmake build -g test",
        --     "xmake run -g test"
        -- )
    end)
end

task("cpcm")
do
    set_menu {
        usage = "xmake cpcm"
    }
    on_run(function()
        local cmd     = import("script.cmd")
        -- print(cmd)

        local profile = "debug"
        cmd.exec_cmds(
            "xmake f -c",
            string.format("xmake f -m %s ", profile), --toolchain=llvm",
            "xmake project -k compile_commands"
        )
    end)
end

task("targets")
do
    set_menu {}
    on_run(function()
        for targetname, target in pairs(project.targets()) do
            print(target:targetfile())
        end
    end)
end

task("vscode")
do
    set_menu {}
    on_run(function()
        local cmd     = import("script.cmd")

        local project = import("core.project.project")
        -- print(project)
        for targetname, target in pairs(project.targets()) do
            -- print(targetname)
            -- print(target)
            -- print(target:targetfile())
            -- print(target:basename())
            -- print(target:filename())
            -- print(target:linkname())
            -- print(target:targetdir())
            -- print(target:kind())
            -- return

            -- local target_name = arg[1] or "__DEFAULT_VAR__"
            -- local target_dir = arg[2] or "__DEFAULT_VAR__"
            -- local target_base_name = arg[3] or "__DEFAULT_VAr__"
            if target:kind() == "binary" then
                cmd.run_native_lua("script/vscode.lua", target:name(), target:targetdir(), target:basename(),
                    target:type())
            end
        end
    end)
end


task("t")
do
    set_menu {}
    on_run(function()
        local cmds = import("script.cmd")
        local project = import("core.project.project")
        for targetname, target in pairs(project.targets()) do
            print(targetname)
            -- print(target)
            print(target:targetfile())
            local a, b, c = target:tool("cxx")
            print(a)
            print(b)
            print(c)
            print(c.name)
            print(c.arch)

            print(target:basename())
            print(target:filename())
            print(target:linkname())
            print(target:targetdir())
            print(target:kind())
            break
        end

        --  print("........................")
        --  local ret = cmds.exec_cmds("xmake show -l toolchains")
        --  print(ret)
    end)
end
