-- Pure-logic cases for GridMover. The engine's Lua tests are C++ gtests that
-- host a full scripting system; this file mocks the map and the entity and
-- runs under a stock Lua interpreter. From the repo root:
--   luajit Engine/Test/Lua/GridMoverTest.lua
--
-- Clips are the caller's job. update returns whether this frame is a walk;
-- Player.lua turns that flag plus facing.name into idle_<name> / walk_<name>.

local source = arg and arg[0] or "Engine/Test/Lua/GridMoverTest.lua"
local root = source:gsub("Engine/Test/Lua/GridMoverTest%.lua$", "")
if root == "" or root == source then
    root = "."
end
package.path = root .. "/Engine/Content/Lua/?.lua;" .. package.path

Vec2 = {}
function Vec2.new(x, y)
    return { x = x, y = y }
end

Vec3 = {}
function Vec3.new(x, y, z)
    return { x = x, y = y, z = z }
end

-- GridMover asks the global `world`, not the harness local of that name.
-- `actor(nil)` still has a self table and simply does not define blocksEntry.
world = {}
function world.scriptOf(occupant)
    if occupant == nil then
        return nil
    end
    return occupant.self
end

local GridMover = require("GridMover")

local passed = 0
local failures = 0

local function check(cond, message)
    if cond then
        passed = passed + 1
        return
    end
    failures = failures + 1
    io.stderr:write("FAIL: " .. message .. "\n")
end

local function near(actual, expected)
    return math.abs(actual - expected) < 1e-9
end

-- cellSize 1, map origin at the world origin. Occupants keyed by "x,y".
-- blocksEntry: nil means the script does not answer; false lets the walker
-- through; true blocks. onEnter runs from onPlayerEnter.
local function harness(spec)
    spec = spec or {}
    local position = { x = spec.x or 0.5, y = spec.y or 0.5, z = spec.z or 0.1 }
    local transform = {}
    function transform:getPosition()
        return position
    end
    function transform:setPosition(value)
        position.x = value.x
        position.y = value.y
        position.z = value.z
    end

    local entity = {}
    function entity:getTransform()
        return transform
    end

    local solids = spec.solids or {}
    local occupants = spec.occupants or {}
    local queries = {}
    local map = {}
    function map:worldToCell(value)
        return { x = math.floor(value.x), y = math.floor(value.y) }
    end
    function map:cellToWorld(x, y)
        if spec.cellToWorld then
            return spec.cellToWorld(x, y)
        end
        return { x = x + 0.5, y = y + 0.5 }
    end
    function map:isSolid(x, y)
        return solids[x .. "," .. y] == true
    end
    function map:entityAt(x, y, except)
        queries[#queries + 1] = { x = x, y = y, except = except }
        local occupant = occupants[x .. "," .. y]
        if occupant == nil or occupant == except then
            return nil
        end
        return occupant
    end

    local mover = GridMover:new(entity, {
        map = map,
        stepSeconds = spec.stepSeconds,
        texelsPerUnit = spec.texelsPerUnit,
        facing = spec.facing,
    })
    return {
        mover = mover,
        entity = entity,
        position = position,
        queries = queries,
    }
end

local function actor(blocksEntry, onEnter)
    local calls = {}
    local self = { calls = calls }
    if blocksEntry ~= nil then
        function self:blocksEntry()
            calls[#calls + 1] = "blocksEntry"
            return blocksEntry
        end
    end
    if onEnter then
        function self:onPlayerEnter()
            calls[#calls + 1] = "onPlayerEnter"
            onEnter()
        end
    end
    return { calls = calls, self = self }
end

local right = { dx = 1, dy = 0, name = "right" }
local down = { dx = 0, dy = -1, name = "down" }

local function caseWalksOneOpenCell()
    local world = harness()
    check(world.mover.__className == "GridMover", "built with Class.define")
    check(world.entity.getSpriteAnimation == nil, "the mover does not require an animation component")
    check(world.mover.showPose == nil, "the mover has no clip helper")
    check(near(world.position.x, 0.5) and near(world.position.y, 0.5), "spawn sits on the cell centre")
    check(world.position.z == 0.1, "spawn keeps authored z")

    local walking = world.mover:update(0.1, right)
    check(walking == true, "a started step is a walk frame")
    check(world.mover.cell.x == 0 and world.mover.to.x == 1, "the step leaves cell 0 for cell 1")
    check(near(world.position.x, 1.0) and near(world.position.y, 0.5), "halfway through the 0.2s step")
    check(world.mover.facing.name == "right", "the step faces the caller's direction")

    walking = world.mover:update(0.1, nil)
    check(walking == true, "the landing frame is still a walk frame")
    check(world.mover.cell.x == 1 and world.mover.to == nil, "the step lands on cell 1 and stops")
    check(near(world.position.x, 1.5) and near(world.position.y, 0.5), "landed on the next cell centre")
    check(world.position.z == 0.1, "z is unchanged by the step")

    walking = world.mover:update(0.1, nil)
    check(walking == false, "the following frame is standing")
    check(near(world.position.x, 1.5), "standing stays on the landed cell")
end

local function caseSolidBlocks()
    local world = harness({ solids = { ["1,0"] = true } })
    local walking = world.mover:update(0.1, right)
    check(walking == false, "a refused step is not a walk frame")
    check(world.mover.cell.x == 0 and world.mover.to == nil, "a solid cell is not entered")
    check(near(world.position.x, 0.5), "position stays on the current cell")
    check(world.mover.facing.name == "right", "facing turns even when the step is refused")
    local sawEntity = false
    for _, query in ipairs(world.queries) do
        if query.x == 1 then
            sawEntity = true
        end
    end
    check(sawEntity == false, "a solid cell is not asked entityAt")
end

local function caseOccupantBlocks()
    local silent = actor(nil)
    local firm = actor(true)
    local world = harness({ occupants = { ["1,0"] = silent } })
    local walking = world.mover:update(0.1, right)
    check(walking == false and world.mover.to == nil, "a missing blocksEntry blocks")
    check(silent.calls[1] == nil, "a missing blocksEntry is not invoked")

    world = harness({ occupants = { ["1,0"] = firm } })
    walking = world.mover:update(0.1, right)
    check(walking == false and world.mover.to == nil, "blocksEntry true blocks")
    check(near(world.position.x, 0.5), "a blocking occupant does not move the walker")
end

local function caseBlocksEntryFalseAllows()
    local door = actor(false)
    local world = harness({ occupants = { ["1,0"] = door } })
    local walking = world.mover:update(0.1, right)
    check(walking == true, "blocksEntry false starts the step")
    check(world.mover.to ~= nil and world.mover.to.x == 1, "the walker enters the open actor's cell")
    check(door.calls[1] == "blocksEntry", "passage still asks blocksEntry")
    local poked = false
    for _, name in ipairs(door.calls) do
        if name == "onPlayerEnter" then
            poked = true
        end
    end
    check(poked == false, "onPlayerEnter waits until the cell is reached")
end

local function caseArrivalPokesOnPlayerEnter()
    local entered = 0
    local door = actor(false, function()
        entered = entered + 1
    end)
    local world = harness({ occupants = { ["1,0"] = door } })
    local walking = world.mover:update(0.2, right)
    check(walking == true, "landing and chaining is still a walk frame")
    check(entered == 1, "reaching the cell calls onPlayerEnter once")
    check(world.mover.cell.x == 1, "arrival commits the cell before the poke")

    local excluded = false
    local pokedWithSelf = false
    for _, query in ipairs(world.queries) do
        if query.x == 1 and query.y == 0 and query.except == world.entity then
            pokedWithSelf = true
        end
        if query.except == world.entity and query.x == 0 then
            excluded = true
        end
    end
    check(pokedWithSelf, "the arrival query excludes the walker")
    check(excluded == false, "the start cell is not poked")

    -- Same-frame chain: dt == stepSeconds and the direction is still held,
    -- so the landing also begins the next step with zero carry.
    check(world.mover.to ~= nil and world.mover.to.x == 2, "a held direction chains with the leftover")
    check(near(world.mover.progress, 0), "an exact step leaves no leftover")
end

local function caseCarryAndFreeze()
    local world = harness()
    -- 0.25 / 0.2 = 1.25: land on cell 1 and carry 0.25 toward cell 2.
    local walking = world.mover:update(0.25, right)
    check(walking == true, "an overflowing step is still walking")
    check(world.mover.cell.x == 1 and world.mover.to.x == 2, "leftover progress starts the next cell")
    check(near(world.mover.progress, 0.25), "carry is progress - 1")
    check(near(world.position.x, 1.75), "position eases into the carried step")

    world.mover:freeze()
    check(near(world.mover.progress, 0), "freeze zeroes progress")
    check(world.mover.to ~= nil and world.mover.to.x == 2, "freeze keeps the in-flight cell")
    check(near(world.position.x, 1.5), "freeze draws the cell the step left")

    walking = world.mover:update(0.1, nil)
    check(walking == true and near(world.mover.progress, 0.5), "the next tick resumes the frozen step")
end

local function caseSnapTexels()
    local world = harness({
        cellToWorld = function(x, y)
            return { x = x + 0.52, y = y + 0.52 }
        end,
    })
    -- snap(0.52) = floor(0.52 * 16 + 0.5) / 16 = 0.5
    check(near(world.position.x, 0.5) and near(world.position.y, 0.5), "spawn snaps to a texel")
    check(world.mover.facing.dy == -1 and world.mover.facing.name == "down", "default facing is down")
end

local function caseNilDirectionDoesNotTurn()
    local world = harness({ facing = down })
    local walking = world.mover:update(0.1, nil)
    check(walking == false, "no direction is a standing frame")
    check(world.mover.facing == down, "nil direction does not replace facing")
end

caseWalksOneOpenCell()
caseSolidBlocks()
caseOccupantBlocks()
caseBlocksEntryFalseAllows()
caseArrivalPokesOnPlayerEnter()
caseCarryAndFreeze()
caseSnapTexels()
caseNilDirectionDoesNotTurn()

if failures > 0 then
    io.stderr:write(string.format("GridMoverTest: %d assertions failed, %d passed\n", failures, passed))
    os.exit(1)
end
print(string.format("GridMoverTest: 8 cases, %d assertions passed", passed))
