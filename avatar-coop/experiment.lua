-- Tries out values found by the finder's label search, to learn what they do.
-- Each number key runs one experiment; watch the game and note what happens.
-- The game is saved when the script starts, and 0 rewinds to that point, so
-- nothing an experiment does can stick.
--
-- Run it while controlling Aang, with Sokka in the party, somewhere with an
-- enemy or two nearby.

local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local mem = dofile(here .. "memory.lua")

local function byte(addr)
  return { addr = addr, size = 1 }
end

local ACTIVE = byte(0x021C7725) -- 0 with Aang, 1 with Sokka
local FLAG_A = byte(0x021EA06C) -- 0 with Aang, 1 with Sokka
local FLAG_B = byte(0x021EA26A) -- 1 with Aang, 0 with Sokka
local INDEX_2 = byte(0x021D3E24) -- 0 with Aang, 1 with Sokka
local INDEX_3 = byte(0x021D3E60) -- 5 with Aang, 4 with Sokka

local function swap01(entry)
  mem.write(entry, mem.read(entry) == 0 and 1 or 0)
end

-- `hold` experiments rewrite their values every frame while switched on;
-- the others change the values once.
local experiments = {
  { name = "Flip 021C7725 (0 <-> 1) once", run = function() swap01(ACTIVE) end },
  {
    name = "Hold both flags ON (021EA06C, 021EA26A = 1)",
    hold = function()
      mem.write(FLAG_A, 1)
      mem.write(FLAG_B, 1)
    end,
  },
  {
    name = "Hold both flags OFF (021EA06C, 021EA26A = 0)",
    hold = function()
      mem.write(FLAG_A, 0)
      mem.write(FLAG_B, 0)
    end,
  },
  {
    name = "Swap the two flags once",
    run = function()
      local a, b = mem.read(FLAG_A), mem.read(FLAG_B)
      mem.write(FLAG_A, b)
      mem.write(FLAG_B, a)
    end,
  },
  { name = "Flip 021D3E24 (0 <-> 1) once", run = function() swap01(INDEX_2) end },
  {
    name = "Flip 021D3E60 (5 <-> 4) once",
    run = function()
      mem.write(INDEX_3, mem.read(INDEX_3) == 5 and 4 or 5)
    end,
  },
}

local watches = {
  { "021C7725", ACTIVE },
  { "021EA06C", FLAG_A },
  { "021EA26A", FLAG_B },
  { "021D3E24", INDEX_2 },
  { "021D3E60", INDEX_3 },
}

-- Number keys, by the names BizHawk may use (the same list as config.lua).
local function digit_names(n)
  return { "D" .. n, "Number" .. n, tostring(n), "NumberPad" .. n, "Keypad" .. n }
end

local start = memorysavestate.savecorestate()
local status = "Saved. Press 1-6 to try an experiment, 0 to rewind."
local held = {}
local previous = {}

local function pressed(keys, n)
  for _, k in ipairs(digit_names(n)) do
    if keys[k] and not previous[k] then
      return true
    end
  end
  return false
end

while true do
  local keys = input.get()
  if pressed(keys, 0) then
    memorysavestate.loadcorestate(start)
    held = {}
    status = "Rewound to the start. All experiments off."
  end
  for i, e in ipairs(experiments) do
    if pressed(keys, i) then
      if e.hold then
        held[i] = not held[i]
        status = string.format("%d: %s - %s", i, e.name, held[i] and "ON" or "off")
      else
        e.run()
        status = string.format("%d: %s - done", i, e.name)
      end
      console.log(status)
    end
  end
  previous = keys
  for i, e in ipairs(experiments) do
    if held[i] then
      e.hold()
    end
  end

  gui.text(2, 2, status)
  local y = 18
  for i, e in ipairs(experiments) do
    gui.text(2, y, string.format("%d%s %s", i, held[i] and "*" or " ", e.name))
    y = y + 14
  end
  y = y + 6
  for _, w in ipairs(watches) do
    gui.text(2, y, string.format("%s = %d", w[1], mem.read(w[2])))
    y = y + 14
  end
  emu.frameadvance()
end
