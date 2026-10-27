-- Shows the live value of every address in config.lua on screen, so you can
-- check that each one changes when it should before running coop.lua.

local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local cfg = dofile(here .. "config.lua")
local mem = dofile(here .. "memory.lua")

local watches = {
  { "P1 x", cfg.p1.x },
  { "P1 y", cfg.p1.y },
}
for _, h in ipairs(cfg.heroes) do
  if h.at then
    local L = cfg.layout
    watches[#watches + 1] = { h.name .. " x", mem.field(h.at, L.x) }
    watches[#watches + 1] = { h.name .. " y", mem.field(h.at, L.y) }
    if L.facing then
      watches[#watches + 1] = { h.name .. " facing", mem.field(h.at, L.facing) }
    end
    if L.pad then
      watches[#watches + 1] = { h.name .. " pad", mem.field(h.at, L.pad, 2) }
    end
  end
end
for name, action in pairs(cfg.actions) do
  watches[#watches + 1] = { "action " .. name, action }
end

local function describe(entry)
  local offset = mem.resolve(entry)
  if not offset then
    return "unavailable (null pointer?)"
  end
  return string.format("%d  @ %08X", mem.read(entry), 0x02000000 + offset)
end

-- Health comes from the cheat codes, so it should always read correctly. If
-- these numbers look wrong, memory reading itself isn't working.
local function party_line(member)
  local hp, max_hp = mem.read(member.hp), mem.read(member.max_hp)
  return string.format("%s HP: %s / %s", member.name, tostring(hp), tostring(max_hp))
end

while true do
  local y = 2
  for _, member in ipairs(cfg.party) do
    gui.text(2, y, party_line(member))
    y = y + 14
  end
  for _, w in ipairs(watches) do
    gui.text(2, y, w[1] .. ": " .. describe(w[2]))
    y = y + 14
  end
  emu.frameadvance()
end
