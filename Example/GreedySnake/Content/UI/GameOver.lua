-- GameOver.lua
-- Document logic for the GameOver entry. Gameplay calls show(score).
-- The Restart button asks the world script to rebuild the board.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local function snake()
    local game = world.find("Game")
    return game and world.scriptOf(game) or nil
end

function Script:onInit()
    self:find("Restart").onClicked:add(self, function()
        self.root.visible = false
        local play = snake()
        if play and play.restart then
            play:restart()
        end
    end)
end

function Script:show(score)
    self.finalScore = score
    self.root.visible = true
end

function Script:hide()
    self.root.visible = false
end

return Script
