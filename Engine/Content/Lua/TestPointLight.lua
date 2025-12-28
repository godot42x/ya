-- OrbitScript.lua
-- 圆周运动脚本

local script = {}

script.radius = 5.0 -- 圆周半径
script.speed = 90.0 -- 角速度 (度/秒)
script.height = 5.0 -- Y轴高度
script.angle = 0.0  -- 当前角度

function script:onInit()
    print("OrbitScript initialized")
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
            local newPos = vec3.new(
                self.radius * math.cos(angleRad),
                self.height,
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
