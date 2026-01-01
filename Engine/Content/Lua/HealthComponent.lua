-- HealthComponent.lua
-- 使用 OOP 的健康组件示例

local Class = require("Class")
local ScriptBase = require("ScriptBase")

-- ✅ 使用 Class 继承
local Health = Class.define("HealthComponent", ScriptBase)

-- 构造函数
function Health:__init()
    ScriptBase.__init(self)
    
    -- 定义属性
    self.maxHealth = 100
    self.currentHealth = 100
    self.regenerationRate = 5.0
    self.isInvincible = false
    
    -- 定义元数据
    self:properties({
        maxHealth = {
            type = "int",
            min = 1,
            max = 10000,
            tooltip = "最大生命值"
        },
        regenerationRate = {
            min = 0,
            max = 100,
            tooltip = "每秒恢复的生命值"
        },
        isInvincible = {
            tooltip = "无敌模式（测试用）"
        }
    })
end

-- 覆盖生命周期方法
function Health:onInit()
    self.currentHealth = self.maxHealth
    print(string.format("Health Component: %d/%d HP", self.currentHealth, self.maxHealth))
end

function Health:onUpdate(dt)
    -- 自动回血
    if self.regenerationRate > 0 and self.currentHealth < self.maxHealth then
        self.currentHealth = math.min(
            self.currentHealth + self.regenerationRate * dt,
            self.maxHealth
        )
    end
end

-- 自定义方法
function Health:takeDamage(amount)
    if self.isInvincible then
        print("Invincible! Damage ignored.")
        return
    end
    
    self.currentHealth = math.max(0, self.currentHealth - amount)
    print(string.format("Took %d damage. HP: %d/%d", amount, self.currentHealth, self.maxHealth))
    
    if self.currentHealth <= 0 then
        self:die()
    end
end

function Health:heal(amount)
    local oldHealth = self.currentHealth
    self.currentHealth = math.min(self.maxHealth, self.currentHealth + amount)
    local healed = self.currentHealth - oldHealth
    print(string.format("Healed %d HP. Current: %d/%d", healed, self.currentHealth, self.maxHealth))
end

function Health:die()
    print("Entity died!")
    -- 可以触发死亡事件、播放动画等
end

return Health
