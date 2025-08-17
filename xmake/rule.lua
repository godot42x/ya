rule("microshit")
do
    on_config(function(target)
        if target:is_plat("windows") then
            if target:get("kind") == "shared" then
                print(target:name(), "with microshit rule >>")
                local normalizeName = target:name():upper():replace("%.", '_')
                print("normalizedName:", normalizeName)
                target:add("defines", "BUILD_SHARED_" .. normalizeName .. "=1")
            end
        end
    end)
end

-- on_load -> after_load -> on_config -> before_build -> on_build -> after_build

for _, files in ipairs(os.files(os.projectdir() .. "/Example/**/*xmake.lua")) do
    -- print("Including xmake.lua from:", files)
    includes(files)
end
-- #pragma once

-- #if _WIN32
--     #ifdef BUILD_SHARED_UTILITY_CC
--         #define UTILITY_API __declspec(dllexport)
--     #else
--         #define UTILITY_API __declspec(dllimport)
--     #endif
-- #else
--     #define UTILITY_API
-- #endif

rule("SourceFiles")
do
    on_load(function(t)
        print(t:sourcefiles())
        print(t:get("files"))
        t:add("files", "Source/**.cpp")

        print(t:get("files"))
        print("sourcefiles", t:get("sourcefiles"))
        print("???")
        print(t:get("scriptdir"))
        print(os.scriptdir())
        -- os.raise()
    end)
end
