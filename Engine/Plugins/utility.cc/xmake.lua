-- 添加 debug 选项
option("utility_debug")
do
    set_default(true)
    set_showmenu(true)
    set_description("Enable debug logging for utility.cc StackDeleter")
end

target("utility.cc")
do
    set_kind("shared")
    set_languages("c++20")
    add_files("./src/**.cpp")
    add_headerfiles("./src/**.h", { public = true })
    add_includedirs("./src/include", { public = true })

    -- 根据 debug 选项设置宏定义
    if has_config("utility_debug") then
        add_defines("UTILITY_DEBUG_ENABLED")
    end

    if is_plat("windows") then
        add_cxflags(
            "/utf-8" --  Enable UTF-8 source code support for Unicode characters
        )
    end

    on_config(function(target)
        if is_host("window") then
            if target:get("kind") == "shared" then
                print(target:name(), "with microshit rule >>")
                local normalizeName = target:name():upper():replace("%.", '_')
                print("normalizedName:", normalizeName)
                target:add("defines", "BUILD_SHARED_" .. normalizeName .. "=1")
            end
        end
    end)
end


do -- grab all cpp file under test folder as a target
    local bDebug = false
    local test_files = os.files(os.scriptdir() .. "/test/*.cpp")
    for _, file in ipairs(test_files) do
        local name = path.basename(file)
        local target_name = "test." .. name
        target(target_name)
        do
            if bDebug then
                print("add test unit:", target_name)
            end
            set_group("test")
            set_kind("binary")
            add_files(file)
            add_deps("utility.cc") -- Ensure the utility.cc is linked
            target_end()
        end
    end
end
