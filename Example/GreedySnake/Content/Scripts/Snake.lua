local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local CELL = 1.0
local HALF = 9
local SPRITE_HALF = 0.45
local STEP_SLOW = 0.42
local STEP_NORMAL = 0.28
local STEP_FAST = 0.16

local segments = {}
local food = nil
local camera = nil
local border = {}
local dir = { x = 1, y = 0 }
local queued = { x = 1, y = 0 }
local accumulator = 0
local stepSeconds = STEP_NORMAL
local score = 0
local alive = true
local settingsOpen = false

local function place(entity, x, y)
    local transform = entity:getTransform()
    if transform then
        transform:setPosition(Vec3.new(x * CELL, y * CELL, 0))
    end
end

local function paint(entity, r, g, b)
    local sprite = entity:getSprite()
    if not sprite then
        return
    end
    sprite.bVisible = true
    sprite.size = Vec2.new(0.9, 0.9)
    sprite.tint = Vec4.new(r, g, b, 1)
end

local function spawnAt(name, x, y, r, g, b)
    local entity = world.spawnSprite(name)
    if not entity then
        log:error("GreedySnake failed to spawn " .. name)
        return nil
    end
    place(entity, x, y)
    paint(entity, r, g, b)
    return { entity = entity, x = x, y = y }
end

local function hitsBody(x, y, ignoreTail)
    local limit = #segments
    if ignoreTail and limit > 0 then
        limit = limit - 1
    end
    for i = 1, limit do
        if segments[i].x == x and segments[i].y == y then
            return true
        end
    end
    return false
end

local function setScoreText(value)
    score = value
    ui.setText("HUD", "Score", "Score: " .. tostring(score))
end

local function showGameOver(show)
    ui.setVisible("GameOver", "GameOver", show)
end

local function showSettings(show)
    settingsOpen = show
    ui.setVisible("Settings", "Settings", show)
end

local function setSpeed(label, seconds)
    stepSeconds = seconds
    ui.setText("Settings", "SpeedValue", "Speed: " .. label)
end

local function clearBodies()
    for _, seg in ipairs(segments) do
        world.destroyEntity(seg.entity)
    end
    segments = {}
    if food then
        world.destroyEntity(food.entity)
        food = nil
    end
end

-- The board is HALF cells around the origin, always. The window only changes
-- how much empty margin is around this rectangle.
local function frameCamera()
    if not camera then
        return
    end
    local aspect = world.viewAspect()
    if aspect < 0.01 then
        aspect = 1
    end
    local span = HALF + 1
    camera.orthoHalfHeight = math.max(span, span / aspect)
end

local function spawnBorder()
    if #border > 0 then
        return
    end
    local thick = 0.16
    local outer = (HALF + SPRITE_HALF) * CELL + thick * 0.5
    local span = (HALF + SPRITE_HALF) * CELL * 2 + thick
    local bars = {
        { "WallTop", 0, outer, span, thick },
        { "WallBottom", 0, -outer, span, thick },
        { "WallLeft", -outer, 0, thick, span },
        { "WallRight", outer, 0, thick, span },
    }
    for _, bar in ipairs(bars) do
        local entity = world.spawnSprite(bar[1])
        if entity then
            local transform = entity:getTransform()
            if transform then
                transform:setPosition(Vec3.new(bar[2], bar[3], 0))
            end
            local sprite = entity:getSprite()
            if sprite then
                sprite.bVisible = true
                sprite.size = Vec2.new(bar[4], bar[5])
                sprite.tint = Vec4.new(0.32, 0.36, 0.44, 1)
            end
            table.insert(border, entity)
        end
    end
end

local function placeFood()
    for _ = 1, 200 do
        local x = math.random(-HALF, HALF)
        local y = math.random(-HALF, HALF)
        if not hitsBody(x, y, false) then
            if food then
                food.x = x
                food.y = y
                place(food.entity, x, y)
            else
                food = spawnAt("Food", x, y, 0.85, 0.25, 0.2)
            end
            return
        end
    end
end

local function restart()
    clearBodies()
    dir = { x = 1, y = 0 }
    queued = { x = 1, y = 0 }
    accumulator = 0
    alive = true
    setScoreText(0)
    showGameOver(false)
    showSettings(false)
    local head = spawnAt("SnakeHead", 0, 0, 0.95, 0.95, 0.95)
    local bodyA = spawnAt("SnakeBody", -1, 0, 0.25, 0.75, 0.35)
    local bodyB = spawnAt("SnakeBody", -2, 0, 0.25, 0.75, 0.35)
    segments = {}
    if head then table.insert(segments, head) end
    if bodyA then table.insert(segments, bodyA) end
    if bodyB then table.insert(segments, bodyB) end
    placeFood()
    log:info("GreedySnake ready, segments=" .. tostring(#segments))
end

local function queueDirection()
    if input:isKeyDown(EKey.Up) and dir.y == 0 then
        queued = { x = 0, y = 1 }
    elseif input:isKeyDown(EKey.Down) and dir.y == 0 then
        queued = { x = 0, y = -1 }
    elseif input:isKeyDown(EKey.Left) and dir.x == 0 then
        queued = { x = -1, y = 0 }
    elseif input:isKeyDown(EKey.Right) and dir.x == 0 then
        queued = { x = 1, y = 0 }
    end
end

local function step()
    if #segments == 0 then
        return
    end
    dir = queued
    local head = segments[1]
    local nx = head.x + dir.x
    local ny = head.y + dir.y
    if nx < -HALF or nx > HALF or ny < -HALF or ny > HALF or hitsBody(nx, ny, true) then
        alive = false
        showGameOver(true)
        log:info("GreedySnake over, score=" .. tostring(score))
        return
    end

    local ate = food and food.x == nx and food.y == ny
    paint(head.entity, 0.25, 0.75, 0.35)
    if ate then
        local born = spawnAt("SnakeHead", nx, ny, 0.95, 0.95, 0.95)
        if born then
            table.insert(segments, 1, born)
        end
        setScoreText(score + 1)
        placeFood()
    else
        local tail = table.remove(segments)
        tail.x = nx
        tail.y = ny
        place(tail.entity, nx, ny)
        paint(tail.entity, 0.95, 0.95, 0.95)
        table.insert(segments, 1, tail)
    end
end

function Script:onInit()
    math.randomseed(1)
    camera = self.entity and self.entity:getCamera()
    if camera then
        camera.projection = CameraProjection.Orthographic
        camera.primary = true
        frameCamera()
    end
    local transform = self.entity and self.entity:getTransform()
    if transform then
        transform:setPosition(Vec3.new(0, 0, 24))
        transform:setRotation(Vec3.new(0, 0, 0))
    end
    ui.get("GameOver"):find("Restart").onClicked:add(self, restart)
    local settings = ui.get("Settings")
    settings:find("Resume").onClicked:add(self, function() showSettings(false) end)
    settings:find("Slow").onClicked:add(self, function() setSpeed("Slow", STEP_SLOW) end)
    settings:find("Normal").onClicked:add(self, function() setSpeed("Normal", STEP_NORMAL) end)
    settings:find("Fast").onClicked:add(self, function() setSpeed("Fast", STEP_FAST) end)
    input:setKeyHandler(function(key, pressed, repeated)
        if key ~= EKey.Escape then
            return false
        end
        if pressed and not repeated then
            showSettings(not settingsOpen)
        end
        return true
    end)
    spawnBorder()
    restart()
end

function Script:onDestroy()
    input:setKeyHandler(nil)
end

function Script:onUpdate(dt)
    frameCamera()
    if not alive or settingsOpen then
        return
    end
    queueDirection()
    accumulator = accumulator + dt
    if accumulator >= stepSeconds then
        accumulator = accumulator - stepSeconds
        if accumulator > stepSeconds then
            accumulator = 0
        end
        step()
    end
end

return Script
