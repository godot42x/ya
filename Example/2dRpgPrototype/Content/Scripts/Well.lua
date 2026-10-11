-- The well north of the square. The charm from the stall wakes it.
local ScriptBase = require("ScriptBase")
local Items = require("Items")
local Quests = require("Quests")
local Script = ScriptBase:new()

Script.dialogueName = "Dialogue"
Script:properties({
    dialogueName = {
        type = "string",
        tooltip = "Game UI entry this well talks through"
    }
})

local LINES_STILL = { "The well is still. A charm from the square, or glass from the house cabinet, would wake it." }
local LINES_AWAKE = { "The well answers. Tiny Town is awake again." }
local LINES_GLASS = { "The glass hums, and the well answers. Tiny Town is awake again." }

function Script:blocksEntry()
    return true
end

function Script:onInteract()
    local charm = Items.count("well_charm") > 0
    local glass = Items.count("well_glass") > 0
    if charm or glass then
        Quests.complete("well")
    end
    local lines = LINES_STILL
    if charm then
        lines = LINES_AWAKE
    elseif glass then
        lines = LINES_GLASS
    end
    local dialogue = ui.get(self.dialogueName)
    if dialogue then
        dialogue:say(lines)
    end
end

return Script
