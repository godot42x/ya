-- 演示编辑器/运行时条件执行的示例脚本

local ScriptBase = require("ScriptBase")
local MathUtils = require("MathUtils")

local Script = ScriptBase:new()

-- 属性定义
Script.debugRadius = 5.0
Script.rotationSpeed = 45.0
Script.showGizmos = true

Script:properties({
    debugRadius = { 
        min = 0.1, 
        max = 50, 
        tooltip = "调试可视化的半径（仅编辑器）" 
    },
    rotationSpeed = { 
        min = 0, 
        max = 360, 
        tooltip = "旋转速度（度/秒）" 
    },
    showGizmos = { 
        tooltip = "是否显示调试辅助线（仅编辑器）" 
    }
})

-- 编辑器专用：初始化调试数据
Script:editorOnly(function(self)
    print("[EditorDebugExample] 编辑器模式初始化 - 准备调试可视化")
    self.editorFrameCount = 0
    self.debugColor = { r = 1.0, g = 0.5, b = 0.0 }
end)

-- 运行时专用：初始化游戏逻辑
Script:runtimeOnly(function(self)
    print("[EditorDebugExample] 运行时模式初始化 - 启动游戏逻辑")
    self.currentAngle = 0.0
end)

function Script:onInit()
    print(string.format("[EditorDebugExample] onInit - IS_EDITOR=%s, IS_RUNTIME=%s", 
                        tostring(IS_EDITOR), tostring(IS_RUNTIME)))
    
    -- 条件执行示例
    self:when(IS_EDITOR, function(self)
        print("  → 编辑器环境检测完成")
    end)
    
    self:when(IS_RUNTIME, function(self)
        print("  → 运行时环境检测完成")
    end)
end

function Script:onUpdate(dt)
    -- 运行时专用：旋转逻辑
    self:runtimeOnly(function(self)
        self.currentAngle = self.currentAngle + self.rotationSpeed * dt
        if self.currentAngle >= 360 then
            self.currentAngle = self.currentAngle - 360
        end
    end)
    
    -- 编辑器专用：调试信息
    self:editorOnly(function(self)
        self.editorFrameCount = self.editorFrameCount + 1
        
        if self.showGizmos and self.editorFrameCount % 60 == 0 then
            print(string.format("[编辑器预览] 调试半径: %.2f, 帧数: %d", 
                                self.debugRadius, self.editorFrameCount))
        end
    end)
end

function Script:onDestroy()
    self:editorOnly(function(self)
        print("[EditorDebugExample] 编辑器清理")
    end)
    
    self:runtimeOnly(function(self)
        print("[EditorDebugExample] 运行时清理")
    end)
end

-- 自定义方法：仅在运行时可用
function Script:getRotationAngle()
    if not IS_RUNTIME then
        return 0.0
    end
    return self.currentAngle
end

return Script
