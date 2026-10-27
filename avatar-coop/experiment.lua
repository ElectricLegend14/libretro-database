-- Tries out values found by the finder's label search, to learn what they do.
-- Each number key runs one experiment; watch the game and note what happens.
-- The game is saved when the script starts, and 0 rewinds to that point, so
-- nothing an experiment does can stick.
--
-- Run it while controlling Aang, with Sokka in the party, somewhere with an
-- enemy or two nearby.

local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local mem = dofile(here .. "memory.lua")

-- Which hero the player controls: 0 with Aang, 1 with Sokka. Changing
-- either of these switches control (found with the label search, confirmed
-- in the game). Key 9 picks which one the experiments use.
local SWITCHES = { { addr = 0x021D3E24, size = 1 }, { addr = 0x021C7725, size = 1 } }
local ACTIVE = SWITCHES[1]

-- DS buttons by the names BizHawk gives them (possibly with "P1 ").
local BUTTONS = { "Up", "Down", "Left", "Right", "A", "B", "X", "Y", "L", "R", "Start", "Select" }
local pad_names = {}
for key in pairs(joypad.get()) do
  for _, name in ipairs(BUTTONS) do
    if key == name or key == "P1 " .. name then
      pad_names[name] = key
    end
  end
end

-- A "tag-in": control goes to the other hero for `frames` frames with
-- `button` pressed (and Player 1's buttons released), then comes back.
local tag = nil

local function start_tag(button, frames)
  local home = mem.read(ACTIVE)
  tag = { button = button, left = frames, home = home, other = home == 0 and 1 or 0 }
end

local function run_tag()
  if not tag then
    return
  end
  if tag.left > 0 then
    mem.write(ACTIVE, tag.other)
    local pad = {}
    for _, name in ipairs(BUTTONS) do
      if pad_names[name] then
        pad[pad_names[name]] = name == tag.button
      end
    end
    joypad.set(pad)
    tag.left = tag.left - 1
  else
    mem.write(ACTIVE, tag.home)
    tag = nil
  end
end

local experiments = {
  { name = "Switch control to the other hero", run = function() mem.write(ACTIVE, mem.read(ACTIVE) == 0 and 1 or 0) end },
  { name = "Other hero presses A for 1 frame, then back", run = function() start_tag("A", 1) end },
  { name = "Other hero presses A for 4 frames, then back", run = function() start_tag("A", 4) end },
  { name = "Other hero presses A for 12 frames, then back", run = function() start_tag("A", 12) end },
  { name = "Other hero presses B for 4 frames, then back", run = function() start_tag("B", 4) end },
  { name = "Other hero presses Y for 4 frames, then back", run = function() start_tag("Y", 4) end },
}

-- Number keys, by the names BizHawk may use (the same list as config.lua).
local function digit_names(n)
  return { "D" .. n, "Number" .. n, tostring(n), "NumberPad" .. n, "Keypad" .. n }
end

local start = memorysavestate.savecorestate()
local status = "Saved. Press 1-6 to try an experiment, 9 to change switch, 0 to rewind."
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
    tag = nil
    status = "Rewound to the start."
  end
  if pressed(keys, 9) and not tag then
    ACTIVE = ACTIVE == SWITCHES[1] and SWITCHES[2] or SWITCHES[1]
    status = string.format("Experiments now use %08X", ACTIVE.addr)
  end
  for i, e in ipairs(experiments) do
    if pressed(keys, i) and not tag then
      e.run()
      status = string.format("%d: %s", i, e.name)
      console.log(status)
    end
  end
  previous = keys
  run_tag()

  gui.text(2, 2, status)
  local y = 18
  for i, e in ipairs(experiments) do
    gui.text(2, y, string.format("%d %s", i, e.name))
    y = y + 14
  end
  gui.text(2, y + 6, string.format("Using switch %08X (9 = change): hero %d%s", ACTIVE.addr, mem.read(ACTIVE),
    tag and " (tagged in)" or ""))
  gui.text(2, y + 20, string.format("021D3E24 = %d   021C7725 = %d", mem.read(SWITCHES[1]), mem.read(SWITCHES[2])))
  emu.frameadvance()
end
