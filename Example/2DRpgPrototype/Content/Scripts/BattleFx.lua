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

-- Labels live one painter layer above the board (map and units are layer 0).
-- Inside a layer an un-y-sorted object paints before every y-sorted one, so a
-- name that shares layer 0 is always covered by the unit sprite standing on the
-- same cell; lifting the whole readout to layer 1 is what keeps it on top.
local LABEL_LAYER = 1

-- One vertical stack per unit, in world units above the entity. The three
-- pieces share one baseline ladder so the gaps are even and read as a block:
--   name        centred at 0.98   (0.34 tall -> spans ~0.81..1.15)
--   hp bar      centred at 0.60   (0.18 tall -> spans ~0.51..0.69)
--   hp numbers  centred on the bar
-- The numbers sit ON the bar (not below it): below collides with the unit's own
-- head, and a number that fits the bar reads as one badge.
local NAME_Y = 0.98
local BAR_Y  = 0.60
-- Wide enough for the longest hp string the roster produces ("999/999") at the
-- number size below, so the text never spills past the bar ends.
local BAR_WIDTH = 0.62
local BAR_HEIGHT = 0.18

-- Number glyphs are sized to fit inside BAR_HEIGHT with the outline: 0.22
-- world units of cap height in an 0.18-tall bar plus a 2px border on each side.
local NUMBER_SIZE = 0.20
local NAME_SIZE = 0.30

-- Outline thickness in font-raster pixels (same unit as fontPixelSize): 2px
-- around a 32px face is a readable pixel-art border. It is NOT a world or
-- tile-texel measurement -- at 16px/unit one texel is ~8 screen pixels, which
-- would smother the glyph.
local OUTLINE = 2.0

local COLORS = {
    barBg      = { 0.13, 0.12, 0.16, 0.92 },
    barHp      = { 0.62, 0.28, 0.28, 1.0 },
    barLow     = { 0.86, 0.32, 0.24, 1.0 },
    barAlly    = { 0.38, 0.62, 0.42, 1.0 },
    damage     = { 1.0, 0.90, 0.50, 1.0 },
    heal       = { 0.55, 1.0, 0.6, 1.0 },
    name       = { 0.94, 0.91, 0.82, 1.0 },
    numbers    = { 0.97, 0.95, 0.88, 1.0 },
    ink        = { 0.09, 0.08, 0.12, 1.0 },
}

function Fx.new(battle)
    return setmetatable({ battle = battle, numbers = {}, labels = {} }, { __index = Fx })
end

local function tint(sprite, color)
    sprite.tint = Vec4.new(color[1], color[2], color[3], color[4])
end

-- One sprite through world.spawnSprite. `size` is a {w, h} table; it crosses
-- as Vec2.new (a nested plain table cannot become a glm::vec2).
function Fx:spawnSprite(name, size, color, sortOrder)
    local entity = world.spawnSprite(name, { texture = WHITE_TEXTURE, size = Vec2.new(size[1], size[2]) })
    if not entity then
        return nil
    end
    local sprite = entity:getSprite2D()
    if sprite then
        sprite.bYSort = false
        sprite.layer = LABEL_LAYER
        sprite.sortOrder = sortOrder or 0
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
                                     fontPixelSize = 32, layer = LABEL_LAYER,
                                     outlineWidth = OUTLINE, outlineColor = Vec4.new(COLORS.ink[1], COLORS.ink[2], COLORS.ink[3], 1.0) })
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
    local ink = Vec4.new(COLORS.ink[1], COLORS.ink[2], COLORS.ink[3], 1.0)
    local label = world.spawnText("UnitName_" .. who, Vec3.new(pos.x, pos.y + NAME_Y, 1.0), who,
                                  { size = NAME_SIZE, color = Vec4.new(COLORS.name[1], COLORS.name[2], COLORS.name[3], 1.0),
                                    fontPixelSize = 32, layer = LABEL_LAYER,
                                    outlineWidth = OUTLINE, outlineColor = ink })
    local numbers = world.spawnText("UnitHp_" .. who, Vec3.new(pos.x, pos.y + BAR_Y, 1.0), "",
                                    { size = NUMBER_SIZE, color = Vec4.new(COLORS.numbers[1], COLORS.numbers[2], COLORS.numbers[3], 1.0),
                                      fontPixelSize = 32, layer = LABEL_LAYER, sortOrder = 2,
                                      outlineWidth = OUTLINE, outlineColor = ink })
    local bg = self:spawnSprite("HpBarBg", { BAR_WIDTH + 0.05, BAR_HEIGHT + 0.06 }, COLORS.barBg, 0)
    local fill = self:spawnSprite("HpBarFill", { BAR_WIDTH, BAR_HEIGHT },
                                  unit.team == "player" and COLORS.barAlly or COLORS.barHp, 1)
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
    local barY = pos.y + BAR_Y
    bar.bg:getTransform():setPosition(Vec3.new(pos.x, barY, 1.0))
    bar.fill:getTransform():setPosition(Vec3.new(pos.x - BAR_WIDTH / 2, barY, 1.01))
    bar.label:getTransform():setPosition(Vec3.new(pos.x, pos.y + NAME_Y, 1.0))
    if bar.numbers then
        -- On the bar, centred: the bar already sits between the name and the
        -- unit, so the number rides it instead of crowding the head.
        bar.numbers:getTransform():setPosition(Vec3.new(pos.x, barY, 1.02))
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
