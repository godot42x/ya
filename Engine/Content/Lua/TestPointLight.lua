-- OrbitScript.lua
-- 圆周运动脚本

local ScriptBase = require("ScriptBase")
local MathUtils = require("MathUtils")

-- ✅ 使用实例模式（不是继承类）
local Script = ScriptBase:new()

-- 定义属性初始值
Script.radius = 5.0
Script.speed = 90.0
Script.angle = 0.0  -- 私有，不暴露

-- 调试：检查环境标志
print(string.format("[TestPointLight] IS_EDITOR=%s, IS_RUNTIME=%s", 
                    tostring(IS_EDITOR), tostring(IS_RUNTIME)))

-- 定义属性元数据（仅编辑器需要）
Script:properties({
    radius = { min = 0.1, max = 100.0, tooltip = "圆周运动的半径" },
    speed = { min = 1.0, max = 360.0, tooltip = "角速度（度/秒）" }
})

function Script:onInit()
    print("OrbitScript initialized")
    self.startPos = self.entity:getTransform():getPosition()
    print("Start Position: ", self.startPos.x, self.startPos.y, self.startPos.z)
end

function Script:onUpdate(dt)
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

function Script:onDestroy()
    print("OrbitScript destroyed")
end

return Script
