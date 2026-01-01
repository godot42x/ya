-- ScriptBase.lua
-- 所有 Lua 脚本的基类（使用 OOP 系统）

local Class = require("Class")

local ScriptBase = Class.define("ScriptBase")

--- 全局运行时环境标识（由 C++ 设置）
--- 仅在未定义时设置默认值，避免覆盖 C++ 的设置
if IS_EDITOR == nil then
    IS_EDITOR = false   -- 编辑器预览模式
end
if IS_RUNTIME == nil then
    IS_RUNTIME = false  -- 运行时模式
end
if IS_SHIPPING == nil then
    IS_SHIPPING = false -- 发布版本
end

--- 构造函数
function ScriptBase:__init()
    -- 初始化实例变量
    self._PROPERTIES = {}
end

--- 仅在编辑器时执行
--- @param func function 要执行的函数
--- @return ScriptBase self 支持链式调用
---
--- 使用示例:
--- Script:editorOnly(function(self)
---     self:properties({ ... })  -- 只在编辑器显示属性
--- end)
function ScriptBase:editorOnly(func)
    if IS_EDITOR then
        func(self)
    end
    return self
end

--- 仅在运行时执行
--- @param func function 要执行的函数
--- @return ScriptBase self 支持链式调用
---
--- 使用示例:
--- Script:runtimeOnly(function(self)
---     print("Game started!")
--- end)
function ScriptBase:runtimeOnly(func)
    if IS_RUNTIME then
        func(self)
    end
    return self
end

--- 条件执行（通用）
--- @param condition boolean 执行条件
--- @param func function 要执行的函数
--- @return ScriptBase self 支持链式调用
function ScriptBase:when(condition, func)
    if condition then
        func(self)
    end
    return self
end

--- 定义可编辑属性（仅编辑器需要）
--- @param metadata table 属性元数据定义
--- @return ScriptBase self 支持链式调用
---
--- 使用示例:
--- Script:properties({
---     speed = { min = 0, max = 100, tooltip = "移动速度" }
--- })
function ScriptBase:properties(metadata)
    -- 运行时跳过属性反射，节省内存和初始化开销
    if IS_SHIPPING then
        return self
    end
    
    for propName, meta in pairs(metadata) do
        local currentValue = self[propName]
        
        if currentValue ~= nil then
            local valueType = type(currentValue)
            local typeHint = "unknown"
            
            if valueType == "number" then
                if meta.type == "int" or (currentValue % 1 == 0 and not meta.type) then
                    typeHint = "int"
                else
                    typeHint = "float"
                end
            elseif valueType == "boolean" then
                typeHint = "bool"
            elseif valueType == "string" then
                typeHint = "string"
            elseif valueType == "table" and currentValue.x and currentValue.y and currentValue.z then
                typeHint = "Vec3"
            end
            
            if meta.type then
                typeHint = meta.type
            end
            
            self._PROPERTIES[propName] = {
                value = currentValue,
                type = typeHint,
                min = meta.min or 0.0,
                max = meta.max or 100.0,
                tooltip = meta.tooltip or ""
            }
        else
            print(string.format("[%s] Warning: Property '%s' not defined", self.__className, propName))
        end
    end
    
    return self
end

--- 快捷别名
ScriptBase.props = ScriptBase.properties
ScriptBase.defineProperties = ScriptBase.properties

-- 默认生命周期回调（子类覆盖）
function ScriptBase:onInit() end
function ScriptBase:onUpdate(dt) end
function ScriptBase:onDestroy() end
function ScriptBase:onEnable() end
function ScriptBase:onDisable() end

return ScriptBase
