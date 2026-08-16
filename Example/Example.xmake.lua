-- Example products. The GUI framework examples (GUIFrameworkSmoke /
-- GUIWorkbench) are standalone GUI binaries that exist in every profile;
-- the 3D project examples are engine-profile products that depend on the
-- full engine aggregate.
if get_config("ya_profile") ~= "gui" then
    includes("./HelloMaterial/xmake.lua")
    includes("./GreedSnake/xmake.lua")
end
includes("./GUIFrameworkSmoke/xmake.lua")
includes("./GUIWorkbench/xmake.lua")
