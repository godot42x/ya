-- PhysicsDemo.lua -- P1 smoke: 2D rigid bodies and a sensor trigger.
--
-- The scene authors a floor (static), a crate above it (dynamic), and a pressure
-- plate (sensor). The crate falls onto the floor and rests there; when it lands
-- over the plate, the sensor reports a contact and this script logs it.
--
-- Positions are read back from TransformComponent, so "the body moved" is the
-- transform the rest of the scene sees.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

Script.crateName = "Crate"
Script:properties({
    crateName = { type = "string", tooltip = "Dynamic body to watch" },
})

function Script:onInit()
    self.crate = world.find(self.crateName)
    self.startY = self.crate and self.crate:getTransform():getPosition().y or 0
    self.logged = false
    self.contactSeen = false
end

function Script:onUpdate(dt)
    if not self.crate then
        return
    end
    local y = self.crate:getTransform():getPosition().y
    if not self.logged and y < self.startY - 0.4 then
        self.logged = true
        log:info("PhysicsDemo: crate fell to y=" .. string.format("%.2f", y))
    end
end

return Script
