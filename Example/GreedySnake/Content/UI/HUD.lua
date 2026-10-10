-- HUD.lua
-- Document logic for the HUD entry. Gameplay calls:
--
--   ui.get("HUD"):setScore(n)
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

function Script:onInit()
    self.score = self:find("Score")
end

function Script:setScore(n)
    self.score.text = "Score: " .. tostring(n)
end

return Script
