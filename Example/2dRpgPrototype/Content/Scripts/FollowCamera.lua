-- Eases the camera toward the player and keeps the framed rectangle inside
-- the map. Zoom and device-pixel snapping live on CameraComponent
-- (resolveCameraViewFraming). This script only follows and clamps.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local TARGET_NAME = "Player"
local MAP_NAME = "TilemapGround"
local FOLLOW_RATE = 6 -- per second; higher follows tighter

function Script:onInit()
    self.camera = self.entity:getCamera()
    self.transform = self.entity:getTransform()
    self.camera.projection = CameraProjection.Orthographic
    self.camera.bPrimary = true
    local position = self.transform:getPosition()
    self.z = position.z
    self.focus = { x = position.x, y = position.y }
    -- The clamp is optional: a scene without the map still follows the player
    -- (useful while a map is being authored, and required once gameplay can
    -- switch scenes).
    local mapEntity = world.find(MAP_NAME)
    self.map = mapEntity and mapEntity:getTilemap() or nil
end

function Script:onUpdate(dt)
    self.target = self.target or world.find(TARGET_NAME)
    if not self.target then
        return
    end

    local halfWidth
    local halfHeight
    if self.camera.pixelPerfect then
        -- (halfWidth, halfHeight) in world units, from the same framing the
        -- projection uses. (0, 0) means the view extent is not ready yet.
        local half = world.viewSize()
        if half.y <= 0 then
            return
        end
        halfWidth = half.x
        halfHeight = half.y
    else
        local view = world.viewSize()
        if view.y < 1 then
            return
        end
        halfHeight = self.camera.orthoHalfHeight
        halfWidth = halfHeight * (view.x / view.y)
    end

    local goal = self.target:getTransform():getPosition()
    local blend = 1 - math.exp(-FOLLOW_RATE * dt)
    self.focus.x = self.focus.x + (goal.x - self.focus.x) * blend
    self.focus.y = self.focus.y + (goal.y - self.focus.y) * blend

    local x = self.focus.x
    local y = self.focus.y
    -- Clamp so the framed rectangle stays inside the map, and centre an axis
    -- the map is too small to fill (otherwise clamping would fight the follow
    -- and jitter at the edge).
    if self.map then
        local bounds = self.map:bounds()
        if bounds.z - bounds.x > 2 * halfWidth then
            x = math.min(math.max(x, bounds.x + halfWidth), bounds.z - halfWidth)
        else
            x = (bounds.x + bounds.z) / 2
        end
        if bounds.w - bounds.y > 2 * halfHeight then
            y = math.min(math.max(y, bounds.y + halfHeight), bounds.w - halfHeight)
        else
            y = (bounds.y + bounds.w) / 2
        end
    end

    self.transform:setPosition(Vec3.new(x, y, self.z))
end

return Script
