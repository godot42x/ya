--print("Check if did not add 'fmt' packages in your toplevel xmake.lua")

if is_plat("windows") then
    add_cxflags("/Zc:preprocessor")
end

target("log.cc")
do
    set_kind("shared")

    set_languages("c++20")

    add_headerfiles("./src/**.h")
    add_files("./src/**.cpp")

    add_includedirs("./src/include/", { public = true })


    LogccHasPrint = false

    on_config(function(target)
        if is_host("window") then
            -- Check if the /Zc:preprocessor flag is not added

            if not LogccHasPrint then
                LogccHasPrint = true
                cprint(
                    '${yellow}' ..
                    '[WARNING] This project/package/plugin which name is [' .. target:name() .. ']' ..
                    'Just add "/Zc:preprocessor" to the whole compiler environment\n' ..
                    '\tBecause you maybe use the msvc.' ..
                    'And the __VA_OPT__ is required by this flag...'
                )
            end

            if target:get("kind") == "shared" then
                print(target:name(), "with microshit rule >>")
                local normalizeName = target:name():upper():replace("%.", '_')
                print("normalizedName:", normalizeName)
                local defines = "BUILD_SHARED_" .. normalizeName .. "=1"
                print("--" .. target:name(), "add defines " .. defines)
                target:add("defines", defines)
            end
        end
    end)
end


target("log.cc.test")
do
    if bDebug then
        print("add test unit:", target_name)
    end
    set_group("test")
    set_kind("binary")
    add_deps("log.cc")
    add_files("./test/**.cpp")
end
