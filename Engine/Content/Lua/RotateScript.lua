-- RotateScript.lua
-- 自动旋转脚本

local script = {}

-- 旋转速度 (度/秒)
script.rotationSpeed = vec3(0, 45, 0)  -- 绕Y轴旋转45度/秒

function script:onInit()
    print("RotateScript initialized")
end

function script:onUpdate(dt)
    local entity = self.entity
    
    if entity and entity:hasTransform() then
        local transform = entity:getTransform()
        if transform then
            local rotation = transform:getRotation()
            
            -- 更新旋转
            rotation.x = rotation.x + self.rotationSpeed.x * dt
            rotation.y = rotation.y + self.rotationSpeed.y * dt
            rotation.z = rotation.z + self.rotationSpeed.z * dt
            
            transform:setRotation(rotation)
        end
    end
end

function script:onDestroy()
    print("RotateScript destroyed")
end

return script
