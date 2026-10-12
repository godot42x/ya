-- WorldTextTest.lua -- WT1 smoke: spawn a couple of world-space labels and,
-- one after the first frame (glyphs rasterize a frame later), keep them alive.
-- Attach it to any entity in a scene to see the engine's world text draw.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

function Script:onInit()
    self.spawned = false
    self.timer = 0
end

function Script:onUpdate(dt)
    if self.spawned then
        return
    end
    -- Wait a beat so the font stack is up and the atlas flush has run.
    self.timer = self.timer + dt
    if self.timer < 0.5 then
        return
    end
    self.spawned = true
    local base = self.entity:getTransform():getPosition()
    world.spawnText("WT_Hello", Vec3.new(base.x, base.y + 1.5, 1.0), "Hello 123",
                    { size = 0.6, color = Vec4.new(1.0, 0.9, 0.4, 1.0), fontPixelSize = 32 })
    world.spawnText("WT_World", Vec3.new(base.x + 2.0, base.y, 1.0), "World",
                    { size = 0.5, color = Vec4.new(0.6, 1.0, 0.7, 1.0), fontPixelSize = 32 })
end

return Script
