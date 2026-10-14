-- BattleFx.lua -- the visual feedback of a battle: floating damage numbers and
-- the name + HP bar over each unit. Plain module (not a script): Battle.lua
-- owns it, calls `showHealth` on every unit and `spawnDamage` on a hit, and
-- drives `update` every frame.
--
-- Text is the engine's world-space text (WT1, `world.spawnText`), so a number
-- is one entity, not a strip of digit sprites. The HP bar itself is a couple
-- of 1x1 white sprites (`white.png`) scaled and tinted.
local Fx = {}

local WHITE_TEXTURE = "Content:Textures/white.png"

local RISE = 0.7            -- world units a damage number climbs
local LIFETIME = 0.8        -- seconds
local TEXT_SIZE = 0.42      -- world height of a damage number

local BAR_WIDTH = 0.7
local BAR_HEIGHT = 0.1
local COLORS = {
    barBg    = { 0.10, 0.09, 0.13, 0.85 },
    barHp    = { 0.35, 0.85, 0.4, 1.0 },
    barLow   = { 0.90, 0.35, 0.30, 1.0 },
    barAlly  = { 0.45, 0.72, 1.0, 1.0 },
    damage   = { 1.0, 0.92, 0.55, 1.0 },
    heal     = { 0.55, 1.0, 0.6, 1.0 },
    name     = { 0.96, 0.96, 0.94, 1.0 },
}

function Fx.new(battle)
    return setmetatable({ battle = battle, numbers = {}, labels = {} }, { __index = Fx })
end

local function tint(sprite, color)
    sprite.tint = Vec4.new(color[1], color[2], color[3], color[4])
end

-- One sprite through world.spawnSprite. `size` is a {w, h} table; it crosses
-- as Vec2.new (a nested plain table cannot become a glm::vec2).
function Fx:spawnSprite(name, size, color)
    local entity = world.spawnSprite(name, { texture = WHITE_TEXTURE, size = Vec2.new(size[1], size[2]) })
    if not entity then
        return nil
    end
    local sprite = entity:getSprite2D()
    if sprite then
        sprite.bYSort = false
        sprite.sortOrder = 100  -- over the board and units
        tint(sprite, color)
    end
    return entity
end

-- A damage number: one world-text entity that rises and fades, then goes away.
-- `worldY` is already lifted above the unit by the caller; this only adds the
-- rise over the number's life.
function Fx:spawnDamage(worldX, worldY, amount, color)
    color = color or COLORS.damage
    local entity = world.spawnText("DamageNumber", Vec3.new(worldX, worldY, 1.0),
                                   tostring(math.max(0, math.floor(amount))),
                                   { size = TEXT_SIZE, color = Vec4.new(color[1], color[2], color[3], 1.0),
                                     fontPixelSize = 32 })
    if entity then
        self.numbers[#self.numbers + 1] = { entity = entity, t = 0, x = worldX, y = worldY, color = color }
    end
end

-- The standing name + HP bar over a unit. `attachBar` is called once per unit;
-- `updateBar` moves it as the unit walks or takes damage.
function Fx:attachBar(unit)
    local key = tostring(unit.entity:getName())
    if self.labels[key] then
        return self.labels[key]
    end
    local pos = unit.transform:getPosition()
    -- The entity name carries the unit's display name: several units stand on
    -- the board at once, and a plain "UnitHp" would be ambiguous to anything
    -- that reads the label back (automation, the inspector).
    local who = tostring(unit.displayName or "?")
    local label = world.spawnText("UnitName_" .. who, Vec3.new(pos.x, pos.y + 1.18, 1.0), who,
                                  { size = 0.48, color = Vec4.new(COLORS.name[1], COLORS.name[2], COLORS.name[3], 1.0),
                                    fontPixelSize = 32 })
    local numbers = world.spawnText("UnitHp_" .. who, Vec3.new(pos.x, pos.y + 0.60, 1.0), "",
                                    { size = 0.36, color = Vec4.new(1.0, 1.0, 0.98, 1.0),
                                      fontPixelSize = 32 })
    local bg = self:spawnSprite("HpBarBg", { BAR_WIDTH + 0.06, BAR_HEIGHT + 0.04 }, COLORS.barBg)
    local fill = self:spawnSprite("HpBarFill", { BAR_WIDTH, BAR_HEIGHT },
                                  unit.team == "player" and COLORS.barAlly or COLORS.barHp)
    if not label or not bg or not fill or not numbers then
        return nil
    end
    -- The fill shrinks from the right, so its pivot is the left edge.
    fill:getSprite2D().pivot = Vec2.new(0.0, 0.5)
    local bar = { label = label, numbers = numbers, bg = bg, fill = fill, unit = unit }
    self.labels[key] = bar
    self:updateBar(unit, bar)
    return bar
end

local function setVisible(entity, visible)
    if not entity then
        return
    end
    local sprite = entity:getSprite2D()
    if sprite then
        sprite.bVisible = visible
    end
    local text = entity:getText2D()
    if text then
        text.bVisible = visible
    end
end

function Fx:updateBar(unit, bar)
    bar = bar or self.labels[tostring(unit.entity:getName())]
    if not bar then
        return
    end
    if not unit:isAlive() then
        setVisible(bar.bg, false)
        setVisible(bar.fill, false)
        setVisible(bar.label, false)
        setVisible(bar.numbers, false)
        return
    end
    local pos = unit.transform:getPosition()
    local barY = pos.y + 0.72
    bar.bg:getTransform():setPosition(Vec3.new(pos.x, barY, 1.0))
    bar.fill:getTransform():setPosition(Vec3.new(pos.x - BAR_WIDTH / 2, barY, 1.01))
    bar.label:getTransform():setPosition(Vec3.new(pos.x, pos.y + 1.18, 1.0))
    if bar.numbers then
        bar.numbers:getTransform():setPosition(Vec3.new(pos.x, pos.y + 0.58, 1.0))
        local text = bar.numbers:getText2D()
        if text then
            text.text = string.format("%d/%d", unit.hp or 0, unit.maxHp or 0)
        end
    end
    local ratio = (unit.maxHp or 0) > 0 and math.max(0.0, unit.hp / unit.maxHp) or 1.0
    bar.fill:getSprite2D().size = Vec2.new(math.max(0.001, BAR_WIDTH * ratio), BAR_HEIGHT)
    if unit.team == "player" then
        tint(bar.fill:getSprite2D(), ratio < 0.34 and COLORS.barLow or COLORS.barAlly)
    else
        tint(bar.fill:getSprite2D(), ratio < 0.34 and COLORS.barLow or COLORS.barHp)
    end
    setVisible(bar.bg, true)
    setVisible(bar.fill, true)
    setVisible(bar.label, true)
    setVisible(bar.numbers, true)
end

-- Re-anchor every bar (the controller calls this when a unit walks).
function Fx:refreshBars(units)
    for _, unit in ipairs(units) do
        self:updateBar(unit)
    end
end

function Fx:update(dt)
    local keep = {}
    for _, number in ipairs(self.numbers) do
        number.t = number.t + dt
        local k = number.t / LIFETIME
        if k >= 1.0 then
            world.destroyEntity(number.entity)
        else
            local text = number.entity:getText2D()
            if text then
                local fade = 1.0 - k * k
                text.color = Vec4.new(number.color[1], number.color[2], number.color[3], fade)
            end
            number.entity:getTransform():setPosition(Vec3.new(number.x, number.y + RISE * k, 1.0))
            keep[#keep + 1] = number
        end
    end
    self.numbers = keep
end

return Fx
