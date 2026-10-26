-- Shows the live value of every address in config.lua on screen, so you can
-- check that each one changes when it should before running coop.lua.

local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local cfg = dofile(here .. "config.lua")
local mem = dofile(here .. "memory.lua")

local watches = {
  { "P1 x", cfg.p1.x },
  { "P1 y", cfg.p1.y },
  { "P2 x", cfg.p2.x },
  { "P2 y", cfg.p2.y },
  { "P2 facing", cfg.p2.facing },
}
for name, action in pairs(cfg.actions) do
  watches[#watches + 1] = { "action " .. name, action }
end

local function describe(entry)
  if entry == nil then
    return "not set"
  end
  local offset = mem.resolve(entry)
  if not offset then
    return "unavailable (null pointer?)"
  end
  return string.format("%d  @ %08X", mem.read(entry), 0x02000000 + offset)
end

while true do
  for i, w in ipairs(watches) do
    gui.text(2, 2 + (i - 1) * 14, w[1] .. ": " .. describe(w[2]))
  end
  emu.frameadvance()
end
