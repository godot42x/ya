-- Actor.lua
-- Conventions every scripted actor (player, NPC, sign) shares.
--
-- Depth: opaque sprites sort by depth, so a character lower on the screen --
-- smaller world y -- draws in front of one higher up. Each actor computes
-- its z as Z_BASE - y * Z_PER_Y; the base sits between the tilemap's Decor
-- and Overlay layers and the slope is small enough that a 20-cell-tall map
-- stays between them (0.03 .. 0.2 here). If a third kind of script grows
-- from this, evaluate an engine component field; until then require this.

local Actor = {}

Actor.Z_BASE  = 0.1   -- the character layer, between Decor and Overlay
Actor.Z_PER_Y = 0.005 -- one cell of height is this much depth

function Actor.zFor(y)
    return Actor.Z_BASE - y * Actor.Z_PER_Y
end

return Actor
