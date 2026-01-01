-- Class.lua
-- Lua 面向对象编程基础库

local Class = {}

--- 创建一个新类
--- @param base table|nil 基类（可选）
--- @return table 新类
function Class.new(base)
    local class = {}
    
    -- 设置元表
    local mt = {
        __index = class,
        __call = function(cls, ...)
            -- 支持 MyClass(...) 语法创建实例
            return cls:new(...)
        end
    }
    
    -- 如果有基类，继承基类
    if base then
        setmetatable(class, {
            __index = base,
            __call = mt.__call
        })
    else
        setmetatable(class, mt)
    end
    
    -- 默认构造函数（子类可覆盖）
    function class:new(...)
        local instance = setmetatable({}, {__index = self})
        
        -- 调用构造函数
        if instance.__init then
            instance:__init(...)
        end
        
        return instance
    end
    
    -- 类型检查
    function class:instanceof(checkClass)
        local meta = getmetatable(self)
        while meta do
            if meta.__index == checkClass then
                return true
            end
            meta = getmetatable(meta.__index)
        end
        return false
    end
    
    -- 类名（可选设置）
    class.__className = "Class"
    
    return class
end

--- 快捷创建类的语法糖
--- @param name string 类名
--- @param base table|nil 基类
--- @return table 新类
function Class.define(name, base)
    local class = Class.new(base)
    class.__className = name
    return class
end

return Class
