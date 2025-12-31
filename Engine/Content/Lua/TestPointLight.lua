-- OrbitScript.lua
-- 圆周运动脚本

local script = {}

script.radius = 5.0 -- 圆周半径
script.speed = 90.0 -- 角速度 (度/秒)
script.angle = 0.0  -- 当前角度

function script:onInit()
    print("OrbitScript initialized")
    self.startPos = self.entity:getTransform():getPosition()
    print("Start Position: ", self.startPos.x, self.startPos.y, self.startPos.z)
end

function script:onUpdate(dt)
    local entity = self.entity

    if entity and entity:hasTransform() then
        local transform = entity:getTransform()
        if transform then
            -- 更新角度
            self.angle = self.angle + self.speed * dt

            -- 计算新位置
            local angleRad = math.rad(self.angle)
            local newPos = self.startPos + Vec3.new(
                self.radius * math.cos(angleRad),
                0,
                self.radius * math.sin(angleRad)
            )

            transform:setPosition(newPos)
        end
    end
end

function script:onDestroy()
    print("OrbitScript destroyed")
end

return script
