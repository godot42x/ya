-- A villager: stands on its cell and answers onInteract by turning toward
-- the player. Its cell blocks the player through map:entityAt, so it reads
-- as a body on the map without any collision code here. Real dialog waits
-- for the dialogue box (R2b).
local ScriptBase = require("ScriptBase")
local Actor = require("Actor")
local Script = ScriptBase:new()

-- hero_walk rows: 0 down, 1 left, 2 right, 3 up.
local ROWS = { down = 0, left = 1, right = 2, up = 3 }

function Script:showFrame(row)
    self.sprite.uvRect = Actor.heroFrame(1, row)
end

function Script:onInit()
    self.transform = self.entity:getTransform()
    self.sprite = self.entity:getSprite2D()
    local position = self.transform:getPosition()
    -- Static actors claim their depth once; only walkers recompute per step.
    self.transform:setPosition(Vec3.new(position.x, position.y, Actor.zFor(position.y)))
    self:showFrame(ROWS.down)
end

function Script:onInteract()
    -- Turn toward whoever knocked: the caller passes nothing, so the player
    -- is found the same way the camera finds the player (world.find).
    local player = world.find("Player")
    if player then
        local mine = self.transform:getPosition()
        local theirs = player:getTransform():getPosition()
        local dx, dy = theirs.x - mine.x, theirs.y - mine.y
        local row = ROWS.down
        if math.abs(dx) > math.abs(dy) then
            row = dx > 0 and ROWS.right or ROWS.left
        elseif dy ~= 0 then
            row = dy > 0 and ROWS.up or ROWS.down
        end
        self:showFrame(row)
    end
    print("[Npc] Welcome to Tiny Town! The sign by the road knows more.")
end

return Script
