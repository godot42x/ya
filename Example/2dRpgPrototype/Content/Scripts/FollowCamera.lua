-- Eases the camera toward the player and frames the view pixel-perfectly:
-- every texel covers a whole number of screen pixels.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local TARGET_NAME = "Player"
local TEXELS_PER_UNIT = 16
local MIN_VISIBLE_TILES = 12 -- vertically; the zoom is the largest that still shows this many
local FOLLOW_RATE = 6 -- per second; higher follows tighter

function Script:onInit()
    self.camera = self.entity:getCamera()
    self.transform = self.entity:getTransform()
    self.camera.projection = CameraProjection.Orthographic
    self.camera.bPrimary = true
    local position = self.transform:getPosition()
    self.z = position.z
    self.focus = { x = position.x, y = position.y }
end

function Script:onUpdate(dt)
    self.target = self.target or world.find(TARGET_NAME)
    local view = world.viewSize()
    if not self.target or view.y < 1 then
        return
    end

    local zoom = math.max(1, math.floor(view.y / (TEXELS_PER_UNIT * MIN_VISIBLE_TILES)))
    local pixelsPerUnit = TEXELS_PER_UNIT * zoom
    self.camera.orthoHalfHeight = view.y / (2 * pixelsPerUnit)

    local goal = self.target:getTransform():getPosition()
    local blend = 1 - math.exp(-FOLLOW_RATE * dt)
    self.focus.x = self.focus.x + (goal.x - self.focus.x) * blend
    self.focus.y = self.focus.y + (goal.y - self.focus.y) * blend

    -- Whole screen pixels, so the world does not shimmer while the camera eases.
    local x = math.floor(self.focus.x * pixelsPerUnit + 0.5) / pixelsPerUnit
    local y = math.floor(self.focus.y * pixelsPerUnit + 0.5) / pixelsPerUnit
    self.transform:setPosition(Vec3.new(x, y, self.z))
end

return Script
