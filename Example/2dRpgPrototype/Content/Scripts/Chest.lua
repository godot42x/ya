-- A storage pot: opening it is remembered across scene switches in the
-- persistent table, keyed by the entity's name (rpg R3). Closed is the
-- lidded pot (tile 107), open the golden one (tile 94).
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local ATLAS_COLUMNS = 12
local ATLAS_ROWS = 11
local CLOSED_TILE = 107
local OPEN_TILE = 94

local LINES_OPENED = { "You opened the storage pot." }
local LINES_EMPTY = { "The pot is empty now." }

function Script:showTile(tile)
    local col = tile % ATLAS_COLUMNS
    local row = math.floor(tile / ATLAS_COLUMNS)
    local u0 = col / ATLAS_COLUMNS
    local v0 = row / ATLAS_ROWS
    self.sprite.uvRect = Vec4.new(u0, v0, u0 + 1 / ATLAS_COLUMNS, v0 + 1 / ATLAS_ROWS)
end

function Script:bOpened()
    return Persist.opened ~= nil and Persist.opened[self.entity:getName()] == true
end

function Script:applyState()
    self:showTile(self:bOpened() and OPEN_TILE or CLOSED_TILE)
end

-- A pot blocks the cell it stands on; the player opens it from the front.
function Script:blocksEntry()
    return true
end

function Script:onInit()
    self.sprite = self.entity:getSprite2D()
    -- The `Persist` global is bound by the script state; the switch-scene
    -- transfer never replaces it, only leaving play does.
    Persist.opened = Persist.opened or {}
    self:applyState()
end

function Script:onInteract()
    local bWasOpened = self:bOpened()
    if not bWasOpened then
        Persist.opened[self.entity:getName()] = true
        self:applyState()
    end
    local dialogue = ui.get("Dialogue")
    if dialogue then
        dialogue:say(bWasOpened and LINES_EMPTY or LINES_OPENED)
    end
end

return Script
