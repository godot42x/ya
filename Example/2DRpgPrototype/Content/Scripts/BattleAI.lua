-- BattleAI.lua -- the enemy's turn, as data the battle controller can play
-- through its existing move / attack steps. A plan is:
--
--   { move = {x, y} | nil,     -- cell to walk to (nil = stay)
--     attack = <unit> | nil }  -- target to strike after moving
--
-- The policy is intentionally small and readable: close on the nearest living
-- opponent, then strike if the walk ended inside attack range. It reads the
-- board only through the battle controller's own queries (reachable, units),
-- so an enemy and a player are driven by the same movement and damage code.
local AI = {}

local function distance(a, b)
    return math.abs(a.x - b.x) + math.abs(a.y - b.y)
end

-- Nearest living unit on the other team. Ties break by the board order the
-- controller already sorted, so the choice is deterministic.
function AI.nearestOpponent(battle, unit)
    local best, bestD
    for _, other in ipairs(battle.units) do
        if other:isAlive() and other.team ~= unit.team then
            local d = distance(unit.cell, other.cell)
            if bestD == nil or d < bestD then
                best, bestD = other, d
            end
        end
    end
    return best
end

-- The best reachable cell to stand on to attack `target`: the one whose
-- distance to the target is smallest (so the unit closes as far as it can).
-- Returns nil when staying put is already as good (the unit is in range).
function AI.approachCell(battle, unit, target)
    if not target then
        return nil
    end
    if distance(unit.cell, target.cell) <= (unit.range or 1) then
        return nil  -- already in range: no need to move
    end
    local cells = battle:reachable(unit)
    local best, bestD
    for _, cell in ipairs(cells) do
        local d = distance(cell, target.cell)
        if bestD == nil or d < bestD then
            best, bestD = cell, d
        end
    end
    if bestD ~= nil and bestD < distance(unit.cell, target.cell) then
        return best
    end
    return nil
end

-- Full plan for one enemy turn. `standCell` is where the unit will be after
-- the move (the approach cell, or its current cell).
function AI.plan(battle, unit)
    local target = AI.nearestOpponent(battle, unit)
    local move = AI.approachCell(battle, unit, target)
    local stand = move or unit.cell
    local attack
    if target and distance(stand, target.cell) <= (unit.range or 1) then
        attack = target
    end
    return { move = move, attack = attack }
end

return AI
