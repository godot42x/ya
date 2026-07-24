local ScriptBase = require "ScriptBase"

local Script = ScriptBase:new()

Script.moveSpeed = 8.0
Script.lookSpeed = 45.0

Script:properties({
    moveSpeed = {
        min = 0.1,
        max = 50.0,
        tooltip = "Runtime player move speed"
    },
    lookSpeed = {
        min = 1.0,
        max = 180.0,
        tooltip = "Runtime player look speed"
    }
})

function Script:onInit()
    local transform = self.entity and self.entity:getTransform()
    if transform then
        transform:setPosition(Vec3.new(0.0, 2.0, 8.0))
        transform:setRotation(Vec3.new(0.0, 180.0, 0.0))
    end

    local camera = self.entity and self.entity:getCamera()
    if camera then
        camera.primary = true
        camera.fixedAspectRatio = false
        camera.fov = 45.0
        camera.nearClip = 0.1
        camera.farClip = 1000.0
    end

    log:info("[HelloMaterial] PlayerCamera initialized")
end

local function updateLook(script, transform, dt)
    if not input:isActionDown("look") then
        return
    end

    local delta = input:getMouseDelta()
    if delta.x == 0.0 and delta.y == 0.0 then
        return
    end

    local rotation = transform:getRotation()
    rotation.y = rotation.y - delta.x * script.lookSpeed * dt
    rotation.x = rotation.x - delta.y * script.lookSpeed * dt

    if rotation.x > 89.0 then
        rotation.x = 89.0
    end
    if rotation.x < -89.0 then
        rotation.x = -89.0
    end

    transform:setRotation(rotation)
end

local function updateMove(script, transform, dt)
    local rotation = transform:getRotation()
    local yaw = math.rad(rotation.y)
    local pitch = math.rad(rotation.x)

    local cosPitch = math.cos(pitch)
    local forward = Vec3.new(
        math.sin(yaw) * cosPitch,
        -math.sin(pitch),
        math.cos(yaw) * cosPitch
    )
    local right = Vec3.new(math.cos(yaw), 0.0, -math.sin(yaw))
    local up = Vec3.new(0.0, 1.0, 0.0)

    local direction = Vec3.new(0.0, 0.0, 0.0)
    if input:isActionDown("move_forward") then
        direction = direction + forward
    end
    if input:isActionDown("move_back") then
        direction = direction + Vec3.new(-forward.x, -forward.y, -forward.z)
    end
    if input:isActionDown("move_right") then
        direction = direction + right
    end
    if input:isActionDown("move_left") then
        direction = direction + Vec3.new(-right.x, -right.y, -right.z)
    end
    if input:isActionDown("move_up") then
        direction = direction + up
    end
    if input:isActionDown("move_down") then
        direction = direction + Vec3.new(0.0, -1.0, 0.0)
    end

    local len = math.sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z)
    if len <= 0.0001 then
        return
    end

    direction = Vec3.new(direction.x / len, direction.y / len, direction.z / len)
    local position = transform:getPosition()
    position = position + Vec3.new(
        direction.x * script.moveSpeed * dt,
        direction.y * script.moveSpeed * dt,
        direction.z * script.moveSpeed * dt
    )
    transform:setPosition(position)
end

function Script:onUpdate(dt)
    local transform = self.entity and self.entity:getTransform()
    if not transform then
        return
    end

    updateLook(self, transform, dt)
    updateMove(self, transform, dt)
end

return Script
