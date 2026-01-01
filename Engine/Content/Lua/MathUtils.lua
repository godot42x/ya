-- MathUtils.lua
-- 可以被所有脚本通过 require("MathUtils") 导入的工具库

local MathUtils = {}

-- 线性插值
function MathUtils.lerp(a, b, t)
    return a + (b - a) * t
end

-- 角度转弧度
function MathUtils.deg2rad(deg)
    return deg * math.pi / 180.0
end

-- 弧度转角度
function MathUtils.rad2deg(rad)
    return rad * 180.0 / math.pi
end

-- 限制值在范围内
function MathUtils.clamp(value, min, max)
    if value < min then return min end
    if value > max then return max end
    return value
end

-- Vec3 辅助函数
function MathUtils.vec3Distance(a, b)
    local dx = b.x - a.x
    local dy = b.y - a.y
    local dz = b.z - a.z
    return math.sqrt(dx*dx + dy*dy + dz*dz)
end

function MathUtils.vec3Normalize(v)
    local len = math.sqrt(v.x*v.x + v.y*v.y + v.z*v.z)
    if len > 0.0001 then
        return Vec3.new(v.x/len, v.y/len, v.z/len)
    end
    return Vec3.new(0, 0, 0)
end

return MathUtils
