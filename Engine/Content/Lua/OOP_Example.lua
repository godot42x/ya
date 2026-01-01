-- OOP_Example.lua
-- 完整的 OOP 示例：展示类、继承、多态

local Class = require("Class")

-- ========================================
-- 示例1：基础类和继承
-- ========================================

-- 定义动物基类
local Animal = Class.define("Animal")

function Animal:__init(name)
    self.name = name
    self.energy = 100
end

function Animal:eat(food)
    self.energy = self.energy + 10
    print(self.name .. " ate " .. food)
end

function Animal:makeSound()
    print(self.name .. " makes a sound")
end

-- 定义狗类（继承动物）
local Dog = Class.define("Dog", Animal)

function Dog:__init(name, breed)
    Animal.__init(self, name)  -- 调用父类构造
    self.breed = breed
end

function Dog:makeSound()  -- 重写父类方法
    print(self.name .. " barks: Woof!")
end

function Dog:fetch()  -- 新方法
    print(self.name .. " fetches the ball")
end

-- 定义猫类（继承动物）
local Cat = Class.define("Cat", Animal)

function Cat:__init(name, color)
    Animal.__init(self, name)
    self.color = color
end

function Cat:makeSound()  -- 重写父类方法
    print(self.name .. " meows: Meow~")
end

function Cat:scratch()  -- 新方法
    print(self.name .. " scratches the furniture")
end

-- ========================================
-- 示例2：使用示例
-- ========================================

-- 创建实例
local dog = Dog("Buddy", "Golden Retriever")
local cat = Cat("Whiskers", "Orange")

-- 调用方法
dog:eat("bone")
dog:makeSound()
dog:fetch()

cat:eat("fish")
cat:makeSound()
cat:scratch()

-- 类型检查
print("\nType checks:")
print("dog is Animal:", dog:instanceof(Animal))    -- true
print("dog is Dog:", dog:instanceof(Dog))          -- true
print("dog is Cat:", dog:instanceof(Cat))          -- false
print("cat is Animal:", cat:instanceof(Animal))    -- true

-- ========================================
-- 示例3：多态
-- ========================================

local animals = {dog, cat}

print("\nPolymorphism demo:")
for _, animal in ipairs(animals) do
    animal:makeSound()  -- 每个动物发出不同的声音
end

-- ========================================
-- 示例4：ScriptBase 实际应用
-- ========================================

local ScriptBase = require("ScriptBase")

-- 定义移动组件基类
local Movable = Class.define("Movable", ScriptBase)

function Movable:__init()
    ScriptBase.__init(self)
    self.speed = 5.0
    self:properties({
        speed = { min = 0, max = 100, tooltip = "移动速度" }
    })
end

function Movable:move(direction, dt)
    -- 基础移动逻辑
    print(string.format("Moving at speed %.2f", self.speed))
end

-- 定义飞行组件（继承 Movable）
local Flyable = Class.define("Flyable", Movable)

function Flyable:__init()
    Movable.__init(self)
    self.altitude = 0.0
    self:properties({
        altitude = { min = 0, max = 1000, tooltip = "飞行高度" }
    })
end

function Flyable:move(direction, dt)
    -- 覆盖移动逻辑，添加垂直移动
    Movable.move(self, direction, dt)
    print(string.format("Flying at altitude %.2f", self.altitude))
end

function Flyable:takeOff()
    self.altitude = 10.0
    print("Taking off!")
end

-- 创建飞行实例
local bird = Flyable()
bird:takeOff()
bird:move("north", 0.1)

print("\n=== OOP Example Complete ===")

return {
    Animal = Animal,
    Dog = Dog,
    Cat = Cat,
    Movable = Movable,
    Flyable = Flyable
}
