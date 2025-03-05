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
