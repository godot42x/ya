add_rules("mode.debug", "mode.release")
add_requires("glew","glfw", "opengl", "glm","spdlog")
set_languages("c++20")
add_shflags("-fPIC",{force = true})
--set_targetdir("./bin")

if is_mode("debug") then 
    add_cxxflags("-Wall ",{force=true})
    add_cxxflags("-O0")
end


target("config")
    set_kind("headeronly" )
    add_headerfiles("include/GLX/config/*.h")

target("ownkit")
    set_kind("staic")
    add_files("src/OwnKit/*.cc")
    add_includedirs("./include/GLX/")
    add_packages( "glm","glew")

target("logx")
    set_kind("shared")
    add_files("src/logx/*.cc")
    add_includedirs("include/GLX/")
    add_deps("ownkit")
    add_packages("spdlog","ownkit")



-- target("glinternal")
--     set_kind("static")
--     add_files("src/glinternal/*.cc")
--     add_includedirs("./include/GLX/")
--     add_deps("logx")
--     add_packages("glfw","glew", "glm")
    

-- target("GLX")
--     set_kind("binary")
--     add_files("src/main.cc")
--     add_includedirs("./include/GLX/")
--     add_deps("glinternal","logx","ownkit")
--     add_packages("glfw","glew", "glm" ,"spdlog")
