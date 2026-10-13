-- ParticleDemo.lua -- X1 smoke: spawn particles from gameplay. It puts an
-- emitter entity at a fixed spot and bursts into it on a timer, so the log and
-- a screenshot show live particles.
--
-- The emitter entity is created once (world.spawnSprite + ParticleEmitterComponent
-- through the automation plane would need a component API; instead this uses
-- the scene's authored emitter and only bursts into it).
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

Script.emitterName = "SparkEmitter"
Script:properties({
    emitterName = { type = "string", tooltip = "Entity carrying the emitter to burst into" },
})

function Script:onInit()
    self.t = 0
    self.armed = true
end

function Script:onUpdate(dt)
    self.t = self.t + dt
    if self.t >= 0.8 then
        self.t = 0
        local emitter = world.find(self.emitterName)
        if emitter then
            particles.burst(emitter, 12)
            log:info("ParticleDemo: burst")
        end
    end
end

return Script
