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
Script.move = 3          -- cells per turn (used from T2)
Script.range = 1         -- attack reach in cells (used from T3)

Script:properties({
    team        = { type = "string", tooltip = "player or enemy" },
    displayName = { type = "string", tooltip = "Name shown on the turn bar" },
    maxHp       = { type = "float", min = 1, max = 999, tooltip = "Maximum hit points" },
    atk         = { type = "float", min = 0, max = 99, tooltip = "Attack power" },
    def         = { type = "float", min = 0, max = 99, tooltip = "Damage reduction" },
    spd         = { type = "float", min = 1, max = 99, tooltip = "Turn order (higher acts first)" },
    move        = { type = "float", min = 0, max = 20, tooltip = "Move range in cells" },
    range       = { type = "float", min = 1, max = 20, tooltip = "Attack range in cells" },
})

function Script:onInit()
    self.hp = self.maxHp
    self.transform = self.entity:getTransform()
    self.sprite = self.entity:getSprite2D()
    self.baseTint = { self.sprite.tint.x, self.sprite.tint.y, self.sprite.tint.z, self.sprite.tint.w }
end

function Script:isAlive()
    return self.hp > 0
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

return Script
