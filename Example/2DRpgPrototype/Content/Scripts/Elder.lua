-- Elder.lua -- the camp elder. Hands out the first quest and points the party
-- at the ruins. A plain villager otherwise: it stands on its cell and answers
-- onInteract by turning toward the caller and talking through the Dialogue
-- entry.
local ScriptBase = require("ScriptBase")
local Quests = require("Quests")
local Script = ScriptBase:new()

Script.dialogueName = "Dialogue"
Script:properties({
    dialogueName = {
        type = "string",
        tooltip = "Game UI entry this elder talks through"
    }
})

-- Pages are data, rebuilt per talk so the errand follows Persist (rpg R3).
local function lines()
    local errand
    if Quests.state("ruins") == "done" then
        errand = "The ruins are quiet again. Rest, and we will see about the deeper halls."
    else
        errand = {
            text = "Monsters have crept into the ruins east of camp. Make the road safe again.",
            onShow = function()
                Quests.start("ruins")
            end,
        }
    end
    return {
        start = "greet",
        greet = { text = "You are awake. Good -- the camp needs you.", next = "roads" },
        roads = {
            text = "The trail east leads to the ruins. Take care past the crates.",
            choices = {
                { text = "What is out there?", next = "errand" },
                { text = "Who else is with me?", next = "party" },
                { text = "Goodbye" },
            },
        },
        errand = errand,
        party = "Two others woke with you. Speak to them when you are ready.",
    }
end

function Script:face(facing)
    self.anim = self.anim or self.entity:getSpriteAnimation()
    if self.anim then
        self.anim:play("idle_" .. facing)
    end
end

function Script:onInit()
    self.transform = self.entity:getTransform()
end

function Script:onInteract(who)
    if who and who:getTransform() then
        local mine = self.transform:getPosition()
        local theirs = who:getTransform():getPosition()
        local dx, dy = theirs.x - mine.x, theirs.y - mine.y
        if math.abs(dx) > math.abs(dy) then
            self:face(dx > 0 and "right" or "left")
        elseif dy ~= 0 then
            self:face(dy > 0 and "up" or "down")
        end
    end
    local dialogue = ui.get(self.dialogueName)
    if dialogue then
        dialogue:say(lines())
    else
        log:warn("Elder: no Dialogue entry mounted")
    end
end

return Script
