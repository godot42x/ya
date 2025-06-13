-- Test framework library (static lib for now to avoid DLL issues)
target("test.cc")
    set_kind("static")
    set_languages("c++17")
    
    add_includedirs("include")
    add_headerfiles("include/TestFramework.h")
    add_files("src/TestFramework.cpp")
    
    -- Add MSVC preprocessor flag for proper macro expansion
    if is_plat("windows") then
        add_cxxflags("/Zc:preprocessor")
    end

-- Test runner executable with tests included
target("test_runner")
    set_kind("binary")
    set_languages("c++17")
    
    add_deps("test.cc")
    add_includedirs("include")
    
    add_files("src/test_runner.cpp", "src/ExampleTests.cpp", "src/ManualTests.cpp", "src/MacroTests.cpp")
    
    -- Add MSVC preprocessor flag for proper macro expansion
    if is_plat("windows") then
        add_cxxflags("/Zc:preprocessor")
    end

-- VS Code Extension
option("build_vscode_extension")
    set_default(false)
    set_showmenu(true)
    set_description("Build the VS Code extension for test.cc")

task("vscode-extension")
    on_run(function ()
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
    set_menu {
        usage = "xmake vscode-extension",
        description = "Build the VS Code extension for Neon Test"
    }
