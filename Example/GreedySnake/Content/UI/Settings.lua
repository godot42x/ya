-- Settings.lua
-- Document logic for the Settings entry. The entry is modal and pauses the
-- game; this script owns the speed label and tells the world script the step.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local STEP_SLOW = 0.42
local STEP_NORMAL = 0.28
local STEP_FAST = 0.16

local function snake()
    local game = world.find("Game")
    return game and world.scriptOf(game) or nil
end

function Script:apply(label, seconds)
    self.value.text = "Speed: " .. label
    local play = snake()
    if play and play.setStep then
        play:setStep(seconds)
    end
end

function Script:open()
    self.root.visible = true
end

function Script:close()
    self.root.visible = false
end

function Script:onInit()
    self.value = self:find("SpeedValue")
    local function bind(name, label, seconds)
        self:find(name).onClicked:add(self, function()
            self:apply(label, seconds)
        end)
    end
    bind("Slow", "Slow", STEP_SLOW)
    bind("Normal", "Normal", STEP_NORMAL)
    bind("Fast", "Fast", STEP_FAST)
    self:find("Resume").onClicked:add(self, function()
        self:close()
    end)
    self:apply("Normal", STEP_NORMAL)
end

function Script:onCancel()
    self:close()
    return true
end

return Script
