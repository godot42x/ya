# Lua OOP 系统文档

## 核心组件

- **Class.lua** - 类定义、继承、类型检查
- **ScriptBase.lua** - 所有脚本基类，提供属性系统和生命周期

## 快速开始

```lua
local Class = require("Class")
local ScriptBase = require("ScriptBase")

local MyScript = Class.define("MyScript", ScriptBase)

function MyScript:__init()
    ScriptBase.__init(self)
    self.speed = 10.0
    self:properties({
        speed = { min = 1, max = 100, tooltip = "移动速度" }
    })
end

function MyScript:onUpdate(dt)
    -- 游戏逻辑
end

return MyScript
```

## 属性系统

### 支持的类型

| 类型 | 示例 |
|-----|------|
| `float` | `speed = 5.5` |
| `int` | `health = 100` |
| `bool` | `isAlive = true` |
| `string` | `name = "Player"` |
| `Vec3` | `pos = Vec3.new(0, 0, 0)` |

### 元数据定义

```lua
self:properties({
    propertyName = {
        type = "float",      -- 可选，覆盖自动推断
        min = 0.0,
        max = 100.0,
        tooltip = "提示文本"
    }
})
```

## 最佳实践

### ✅ 好的做法

```lua
function MyScript:__init()
    ScriptBase.__init(self)
    self.speed = 10.0         -- 公有属性
    self._internal = 0        -- 私有变量
    self:properties({ speed = {...} })
end
```

### ❌ 避免的做法

```lua
-- 类级别初始化会导致所有实例共享变量
MyScript.speed = 10.0  -- ❌ 不要这样做
```

## 示例项目

- `OOP_Example.lua` - 完整 OOP 演示
- `HealthComponent.lua` - 健康组件
- `TestPointLight.lua` - 圆周运动
- `EditorDebugExample.lua` - 编辑器/运行时条件执行

## API 参考

### ScriptBase 方法

- `:properties(metadata)` - 定义可编辑属性
- `:editorOnly(func)` - 仅在编辑器执行
- `:runtimeOnly(func)` - 仅在运行时执行
- `:when(condition, func)` - 条件执行

### Class 方法

- `Class.define(name, base)` - 创建类
- `:instanceof(class)` - 类型检查

## 环境变量

| 变量 | 说明 | 由谁设置 |
|-----|------|---------|
| `IS_EDITOR` | 编辑器预览模式 | C++ |
| `IS_RUNTIME` | 运行时模式 | C++ |

