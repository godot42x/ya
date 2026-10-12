-- BattleFx.lua -- the visual feedback of a battle: floating damage numbers and
-- the small HP bar over each unit. Plain module (not a script): Battle.lua
-- owns it, calls `attachBar` once per turn and `spawnDamage` on a hit, and
-- drives `update` every frame.
--
-- Two texture atlases cross the script boundary as paths:
--   digits.png  10 glyphs of 8x8 in one row   (damage numbers)
--   white.png   a 1x1 white pixel             (bars, tinted)
local Fx = {}

local DIGITS_TEXTURE = "Content:Textures/digits.png"
local WHITE_TEXTURE = "Content:Textures/white.png"

local DIGIT_COUNT = 10
local DIGIT_SIZE = 0.5      -- world units per glyph (8px at 16 texels/unit)
local RISE = 0.6            -- world units a number climbs
local LIFETIME = 0.7        -- seconds

local BAR_WIDTH = 0.75
local BAR_HEIGHT = 0.12
local BAR_LIFT = 0.7        -- above the unit's cell centre
local COLOR_BAR_BG = { 0.12, 0.1, 0.14, 0.85 }
local COLOR_BAR_HP = { 0.35, 0.85, 0.4, 1.0 }
local COLOR_BAR_LOW = { 0.9, 0.35, 0.3, 1.0 }
local COLOR_DAMAGE = { 1.0, 0.9, 0.55, 1.0 }

function Fx.new(battle)
    return setmetatable({ battle = battle, numbers = {}, bars = {} }, { __index = Fx })
end

local function digitUV(index)
    return index / DIGIT_COUNT, 0.0, (index + 1) / DIGIT_COUNT, 1.0
end

local function setTint(sprite, color)
    sprite.tint = Vec4.new(color[1], color[2], color[3], color[4])
end

-- Spawn a sprite through world.spawnSprite and configure its 2D component.
-- `size` is a plain {w, h} Lua table here; it crosses as Vec2.new, because a
-- nested plain table cannot become a glm::vec2 (the options struct is typed).
function Fx:spawnSprite(name, texture, size, color)
    local entity = world.spawnSprite(name, { texture = texture, size = Vec2.new(size[1], size[2]) })
    if not entity then
        return nil
    end
    local sprite = entity:getSprite2D()
    if sprite then
        sprite.bYSort = false
        sprite.sortOrder = 100  -- always over the board and units
        setTint(sprite, color)
    end
    return entity
end

-- A damage number above a world point: one sprite per digit, side by side.
function Fx:spawnDamage(worldX, worldY, amount, color)
    color = color or COLOR_DAMAGE
    local text = tostring(math.max(0, math.floor(amount)))
    local count = #text
    local total = count * DIGIT_SIZE
    local startX = worldX - total / 2 + DIGIT_SIZE / 2
    local group = { sprites = {}, t = 0, x0 = startX, y0 = worldY }
    for i = 1, count do
        local digit = tonumber(string.sub(text, i, i)) or 0
        local entity = self:spawnSprite("DamageNumber", DIGITS_TEXTURE,
                                        { DIGIT_SIZE, DIGIT_SIZE }, color)
        if entity then
            local sprite = entity:getSprite2D()
            local u0, v0, u1, v1 = digitUV(digit)
            sprite.uvRect = Vec4.new(u0, v0, u1, v1)
            entity:getTransform():setPosition(
                Vec3.new(startX + (i - 1) * DIGIT_SIZE, worldY, 1.0))
            group.sprites[#group.sprites + 1] = entity
        end
    end
    self.numbers[#self.numbers + 1] = group
end

-- One HP bar per unit: a dark backing and a fill that narrows with hp. Both
-- are 1x1 white sprites scaled by size; the fill's pivot moves left so it
-- shrinks from the right.
function Fx:attachBar(unit)
    local key = tostring(unit.entity:getName())
    if self.bars[key] then
        return self.bars[key]
    end
    local bg = self:spawnSprite("HpBarBg", WHITE_TEXTURE, { BAR_WIDTH, BAR_HEIGHT }, COLOR_BAR_BG)
    local fill = self:spawnSprite("HpBarFill", WHITE_TEXTURE, { BAR_WIDTH, BAR_HEIGHT }, COLOR_BAR_HP)
    if not bg or not fill then
        return nil
    end
    local fillSprite = fill:getSprite2D()
    fillSprite.pivot = Vec2.new(0.0, 0.5)
    local bar = { bg = bg, fill = fill }
    self.bars[key] = bar
    self:updateBar(unit, bar)
    return bar
end

function Fx:updateBar(unit, bar)
    bar = bar or self.bars[tostring(unit.entity:getName())]
    if not bar or not unit:isAlive() then
        if bar then
            bar.bg:getSprite2D().bVisible = false
            bar.fill:getSprite2D().bVisible = false
        end
        return
    end
    local pos = unit.transform:getPosition()
    local y = pos.y + BAR_LIFT
    local x = pos.x
    bar.bg:getTransform():setPosition(Vec3.new(x, y, 1.0))
    local ratio = 1.0
    if (unit.maxHp or 0) > 0 then
        ratio = math.max(0.0, unit.hp / unit.maxHp)
    end
    bar.fill:getTransform():setPosition(
        Vec3.new(x - BAR_WIDTH / 2, y, 1.01))
    bar.fill:getSprite2D().size = Vec2.new(math.max(0.001, BAR_WIDTH * ratio), BAR_HEIGHT)
    local low = ratio < 0.34
    setTint(bar.fill:getSprite2D(), low and COLOR_BAR_LOW or COLOR_BAR_HP)
    bar.bg:getSprite2D().bVisible = true
    bar.fill:getSprite2D().bVisible = true
end

-- Re-anchor every bar (called when a unit walks, so the bar follows it).
function Fx:refreshBars(units)
    for _, unit in ipairs(units) do
        self:updateBar(unit)
    end
end

-- Advance numbers: rise, fade, and remove the group when spent.
function Fx:update(dt)
    local keep = {}
    for _, group in ipairs(self.numbers) do
        group.t = group.t + dt
        local k = group.t / LIFETIME
        if k >= 1.0 then
            for _, entity in ipairs(group.sprites) do
                world.destroyEntity(entity)
            end
        else
            local lift = RISE * k
            local alpha = 1.0 - k * k
            for i, entity in ipairs(group.sprites) do
                local sprite = entity:getSprite2D()
                if sprite then
                    sprite.tint = Vec4.new(COLOR_DAMAGE[1], COLOR_DAMAGE[2], COLOR_DAMAGE[3], alpha)
                end
                entity:getTransform():setPosition(
                    Vec3.new(group.x0 + (i - 1) * DIGIT_SIZE, group.y0 + lift, 1.0))
            end
            keep[#keep + 1] = group
        end
    end
    self.numbers = keep
end

return Fx
