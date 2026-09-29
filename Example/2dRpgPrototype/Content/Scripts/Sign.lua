-- A signpost: static, interactive. The plank warms while "read"; showing
-- real text waits for the dialogue box (R2b).
local ScriptBase = require("ScriptBase")
local Actor = require("Actor")
local Script = ScriptBase:new()

local TINT_IDLE = Vec4.new(1.0, 1.0, 1.0, 1.0)
local TINT_READ = Vec4.new(1.0, 0.85, 0.45, 1.0)

function Script:onInit()
    self.sprite = self.entity:getSprite2D()
    local position = self.entity:getTransform():getPosition()
    self.entity:getTransform():setPosition(Vec3.new(position.x, position.y, Actor.zFor(position.y)))
end

function Script:onInteract()
    self.bRead = not self.bRead
    self.sprite.tint = self.bRead and TINT_READ or TINT_IDLE
    print("[Sign] Tiny Town -- drawn with the tile brush, kept by the tilemap.")
end

return Script
