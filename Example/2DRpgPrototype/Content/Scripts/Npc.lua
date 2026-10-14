-- A villager: stands on its cell and answers onInteract by turning toward
-- the caller and talking through the Dialogue entry. Its cell blocks the
-- player through map:entityAt, so it reads as a body on the map without any
-- collision code here.
local ScriptBase = require("ScriptBase")
local Quests = require("Quests")
local Script = ScriptBase:new()

Script.dialogueName = "Dialogue"
Script:properties({
    dialogueName = {
        type = "string",
        tooltip = "Game UI entry this villager talks through"
    }
})

-- Pages are data, rebuilt per talk so the pot errand can follow Persist.
-- `start` is the first key. A choice's `next` is another page; a missing
-- `next` closes the box. Showing `errand` starts the pot quest once.
local function lines()
    local errand
    if Quests.isCompleted("pot") then
        errand = "You already looked in the pot."
    else
        errand = {
            text = "A storage pot in the house. Have a look inside.",
            onShow = function()
                Quests.start("pot")
            end,
        }
    end
    return {
        start = "greet",
        greet = { text = "Welcome to Tiny Town!", next = "roads" },
        roads = {
            text = "Mind the fences -- the map keeps the roads, not me.",
            choices = {
                { text = "Where is the well?", next = "well" },
                { text = "What's in the house?", next = "errand" },
                { text = "Goodbye" },
            },
        },
        well = "The well is north of the square.",
        errand = errand,
    }
end

-- The entity's SpriteAnimationComponent has one standing clip per facing.
function Script:face(facing)
    self.anim:play("idle_" .. facing)
end

function Script:onInit()
    self.transform = self.entity:getTransform()
    self.anim = self.entity:getSpriteAnimation()
    self:face("down")
end

function Script:onInteract(who)
    if who then
        local mine = self.transform:getPosition()
        local theirs = who:getTransform():getPosition()
        local dx, dy = theirs.x - mine.x, theirs.y - mine.y
        local facing = "down"
        if math.abs(dx) > math.abs(dy) then
            facing = dx > 0 and "right" or "left"
        elseif dy ~= 0 then
            facing = dy > 0 and "up" or "down"
        end
        self:face(facing)
    end
    local dialogue = ui.get(self.dialogueName)
    if dialogue then
        dialogue:say(lines())
    else
        log:warn("Npc: no Dialogue entry mounted")
    end
end

return Script
