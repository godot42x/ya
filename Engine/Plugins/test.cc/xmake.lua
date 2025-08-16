-- Test framework library (static lib for now to avoid DLL issues)
target("test.cc")
do
    set_kind("static")
    set_languages("c++17")

    add_includedirs("include")
    add_files("src/*.cpp")
    remove_files("src/test_runner.cpp") -- Exclude test runner from the library

    if is_plat("windows") then
        add_cxxflags("/Zc:preprocessor")
    end
end

-- Test runner executable with tests included
target("test-runner")
do
    set_kind("binary")
    set_languages("c++17")
    add_deps("test.cc")
    add_includedirs("include")
    add_files("src/*.cpp")
    -- Add MSVC preprocessor flag for proper macro expansion
    if is_plat("windows") then
        add_cxxflags("/Zc:preprocessor")
    end
end


task("vscode-extension")
do
    set_menu {
        usage = "xmake vscode-extension",
        description = "Build the VS Code extension for ya Test"
    }
    on_run(function()
        import("core.project.task")
        import("utils.progress")

        local extension_dir = path.join(os.projectdir(), "vscode")
        progress.show(30, "Installing dependencies for VS Code extension...")
        os.cd(extension_dir)
        os.exec("npm install")

        progress.show(70, "Building the VS Code extension...")
        os.exec("npm run compile")

        progress.show(100, "VS Code extension build completed.")
        print("You can now open the extension in VS Code with:")
        print("code " .. extension_dir)
    end)
end


includes("./example")