-- The cabinet on the left of the house. The brass key from the pot opens
-- it once. The shard inside is the other way to wake the well.
local ScriptBase = require("ScriptBase")
local Items = require("Items")
local Quests = require("Quests")
local Script = ScriptBase:new()

Script.dialogueName = "Dialogue"
Script:properties({
    dialogueName = {
        type = "string",
        tooltip = "Game UI entry this cabinet talks through"
    }
})

local LINES_LOCKED = { "The cabinet is locked. The brass key from the pot fits this lock." }
local LINES_OPENED = { "The lock turns. A shard of well glass was waiting inside." }
local LINES_EMPTY = { "The cabinet is empty now." }

function Script:bOpened()
    return Persist.opened ~= nil and Persist.opened[self.entity:getName()] == true
end

function Script:applyState()
    self.anim:play(self:bOpened() and "open" or "closed")
end

function Script:blocksEntry()
    return true
end

function Script:onInit()
    self.anim = self.entity:getSpriteAnimation()
    Persist.opened = Persist.opened or {}
    self:applyState()
end

function Script:onInteract()
    local lines = LINES_EMPTY
    if not self:bOpened() then
        if Items.count("brass_key") < 1 then
            lines = LINES_LOCKED
            Quests.start("cabinet")
        else
            Items.take("brass_key", 1)
            Items.give("well_glass", 1)
            Persist.opened[self.entity:getName()] = true
            self:applyState()
            Quests.complete("cabinet")
            lines = LINES_OPENED
        end
    end
    local dialogue = ui.get(self.dialogueName)
    if dialogue then
        dialogue:say(lines)
    else
        log:warn("Cabinet: no Dialogue entry mounted")
    end
end

return Script
