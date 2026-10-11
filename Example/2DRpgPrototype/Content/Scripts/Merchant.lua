-- The square's stall. Talking starts the well errand. Buying spends coins
-- from the pot; the charm is hung at the well.
local ScriptBase = require("ScriptBase")
local Quests = require("Quests")
local Shop = require("Shop")
local Script = ScriptBase:new()

Script.dialogueName = "Dialogue"
Script:properties({
    dialogueName = {
        type = "string",
        tooltip = "Game UI entry this merchant talks through"
    }
})

local function lines()
    local bought = { text = "" }
    function bought.onShow()
        local result = Shop.buy("well_charm")
        if result == "ok" then
            bought.text = "Here. Hang it at the well, north of the square."
        elseif result == "have" then
            bought.text = "You already carry the charm."
        else
            bought.text = "Three coins. Look in the storage pot."
        end
    end
    return {
        start = "counter",
        counter = {
            text = "The well has gone quiet. The charm is three coins.",
            onShow = function()
                Quests.start("well")
            end,
            choices = {
                { text = "I'll buy the charm.", next = "bought" },
                { text = "Not today." },
                { text = "What is the key for?", next = "key" },
            },
        },
        bought = bought,
        key = "That key fits the cabinet on the left side of the house, not my till. The glass in there wakes the well too.",
    }
end

function Script:face(facing)
    self.anim:play("idle_" .. facing)
end

function Script:blocksEntry()
    return true
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
        log:warn("Merchant: no Dialogue entry mounted")
    end
end

return Script
