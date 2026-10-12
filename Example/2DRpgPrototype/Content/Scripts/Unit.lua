-- Unit.lua -- one combatant on the battle board. Stats are authored as script
-- properties (Inspector / scene propertyOverrides); runtime HP lives here and
-- is read by Battle.lua when it collects the roster.
--
-- The board is the unit registry: Battle.lua walks the tilemap grid and asks
-- map:entityAt(x, y) for whoever stands there, then reads that script's
-- `team` / `spd` / `hp`. No separate unit list to keep in sync.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

Script.team = "player"   -- "player" or "enemy"
Script.displayName = "Unit"
Script.maxHp = 20
Script.atk = 5
Script.def = 1
Script.spd = 10
Script.move = 3          -- cells per turn
Script.range = 1         -- basic attack reach in cells
Script.maxMp = 0         -- 0 = no skill slot on the action menu
Script.skillName = ""    -- empty = no skill
Script.skillPower = 0    -- damage before defence
Script.skillCost = 0     -- mp spent per use
Script.skillRange = 1    -- skill reach in cells

Script:properties({
    team        = { type = "string", tooltip = "player or enemy" },
    displayName = { type = "string", tooltip = "Name shown on the turn bar" },
    maxHp       = { type = "float", min = 1, max = 999, tooltip = "Maximum hit points" },
    atk         = { type = "float", min = 0, max = 99, tooltip = "Attack power" },
    def         = { type = "float", min = 0, max = 99, tooltip = "Damage reduction" },
    spd         = { type = "float", min = 1, max = 99, tooltip = "Turn order (higher acts first)" },
    move        = { type = "float", min = 0, max = 20, tooltip = "Move range in cells" },
    range       = { type = "float", min = 1, max = 20, tooltip = "Attack range in cells" },
    maxMp       = { type = "float", min = 0, max = 999, tooltip = "Mana pool; 0 hides the skill" },
    skillName   = { type = "string", tooltip = "Active skill name; empty means none" },
    skillPower  = { type = "float", min = 0, max = 999, tooltip = "Skill damage before defence" },
    skillCost   = { type = "float", min = 0, max = 999, tooltip = "Mana spent per skill use" },
    skillRange  = { type = "float", min = 1, max = 20, tooltip = "Skill range in cells" },
})

-- Manhattan distance between two cells.
local function cellDistance(a, b)
    return math.abs(a.x - b.x) + math.abs(a.y - b.y)
end

Script.cellDistance = cellDistance

function Script:onInit()
    self.hp = self.maxHp
    self.mp = self.maxMp
    self.transform = self.entity:getTransform()
    self.sprite = self.entity:getSprite2D()
    self.baseTint = { self.sprite.tint.x, self.sprite.tint.y, self.sprite.tint.z, self.sprite.tint.w }
end

function Script:isAlive()
    return self.hp > 0
end

function Script:hasSkill()
    return self.skillName ~= nil and self.skillName ~= "" and (self.maxMp or 0) > 0
end

function Script:canUseSkill()
    return self:hasSkill() and self.mp >= (self.skillCost or 0)
end

-- Battle.lua paints the active unit by brightening its sprite; restore puts the
-- authored tint back. Kept here so the unit owns its own look.
function Script:setActive(active)
    if active then
        self.sprite.tint = Vec4.new(1.0, 1.0, 0.6, 1.0)
    else
        local c = self.baseTint
        self.sprite.tint = Vec4.new(c[1], c[2], c[3], c[4])
    end
end

function Script:takeDamage(amount)
    self.hp = math.max(0, self.hp - amount)
    if self.hp <= 0 and self.sprite then
        self.sprite.bVisible = false
    end
    return self.hp
end

-- Damage before defence, never below 1. Deterministic: no RNG, so a battle
-- plays the same way twice and an automation run can assert exact HP.
function Script:damageAgainst(power, target)
    return math.max(1, math.floor(power - (target.def or 0)))
end

return Script

