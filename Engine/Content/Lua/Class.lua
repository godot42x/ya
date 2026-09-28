-- Class.lua
-- Lua 面向对象编程基础库

local Class = {}

--- 创建一个新类
--- @param base table|nil 基类（可选）
--- @return table 新类
function Class.new(base)
    local class = {}
    
    -- 支持 MyClass(...) 语法创建实例
    local function construct(cls, ...)
        return cls:new(...)
    end

    -- 根类不能 __index 到自身：查一个不存在的字段会无限循环，而不是返回 nil
    setmetatable(class, {
        __index = base,
        __call = construct
    })
    
    -- 默认构造函数（子类可覆盖）
    ---@generic T:table
    ---@param  self T
    ---@return T
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
