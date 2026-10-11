-- A storage pot: opening it is remembered across scene switches in the
-- persistent table, keyed by the entity's name (rpg R3). The entity's
-- SpriteAnimationComponent has two one-frame clips: "closed" (the lidded pot)
-- and "open" (the golden one).
local ScriptBase = require("ScriptBase")
local Items = require("Items")
local Quests = require("Quests")
local Script = ScriptBase:new()

Script.dialogueName = "Dialogue"
Script:properties({
    dialogueName = {
        type = "string",
        tooltip = "Game UI entry this pot talks through"
    }
})

local LINES_OPENED = { "You opened the storage pot. Along with some coins, a brass key was inside." }
local LINES_EMPTY = { "The pot is empty now." }

function Script:bOpened()
    return Persist.opened ~= nil and Persist.opened[self.entity:getName()] == true
end

function Script:applyState()
    self.anim:play(self:bOpened() and "open" or "closed")
end

-- A pot blocks the cell it stands on; the player opens it from the front.
function Script:blocksEntry()
    return true
end

function Script:onInit()
    self.anim = self.entity:getSpriteAnimation()
    -- The `Persist` global is bound by the script state; the switch-scene
    -- transfer never replaces it, only leaving play does.
    Persist.opened = Persist.opened or {}
    self:applyState()
end

function Script:onInteract()
    local bWasOpened = self:bOpened()
    if not bWasOpened then
        Persist.opened[self.entity:getName()] = true
        Items.give("brass_key", 1)
        Items.give("coin", 5)
        Quests.complete("pot")
        self:applyState()
    end
    local dialogue = ui.get(self.dialogueName)
    if dialogue then
        dialogue:say(bWasOpened and LINES_EMPTY or LINES_OPENED)
    end
end

return Script
