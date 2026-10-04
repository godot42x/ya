-- A villager: stands on its cell and answers onInteract by turning toward
-- the player and talking through the Dialogue entry. Its cell blocks the
-- player through map:entityAt, so it reads as a body on the map without any
-- collision code here.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local LINES = {
    "Welcome to Tiny Town!",
    "Mind the fences -- the map keeps the roads, not me.",
}

-- The entity's SpriteAnimationComponent has one standing clip per facing.
function Script:face(facing)
    self.anim:play("idle_" .. facing)
end

function Script:onInit()
    self.transform = self.entity:getTransform()
    self.anim = self.entity:getSpriteAnimation()
    self:face("down")
end

function Script:onInteract()
    -- Turn toward whoever knocked: the caller passes nothing, so the player
    -- is found the same way the camera finds the player (world.find).
    local player = world.find("Player")
    if player then
        local mine = self.transform:getPosition()
        local theirs = player:getTransform():getPosition()
        local dx, dy = theirs.x - mine.x, theirs.y - mine.y
        local facing = "down"
        if math.abs(dx) > math.abs(dy) then
            facing = dx > 0 and "right" or "left"
        elseif dy ~= 0 then
            facing = dy > 0 and "up" or "down"
        end
        self:face(facing)
    end
    local dialogue = ui.get("Dialogue")
    if dialogue then
        dialogue:say(LINES)
    else
        log:warn("Npc: no Dialogue entry mounted")
    end
end

return Script
