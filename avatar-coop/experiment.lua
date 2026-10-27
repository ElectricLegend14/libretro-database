-- Tries out values found by the finder's label search, to learn what they do.
-- Each number key runs one experiment; watch the game and note what happens.
-- The game is saved when the script starts, and 0 rewinds to that point, so
-- nothing an experiment does can stick.
--
-- Run it while controlling Aang, with Sokka in the party, somewhere with an
-- enemy or two nearby.

local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local cfg = dofile(here .. "config.lua")
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

-- A "tag-in": control goes to the other hero, `button` is pressed for
-- `frames` frames once the switch has really happened, then control comes
-- back. The switch takes the game a few frames; it has happened when Player
-- 1's fixed position copy jumps to the other hero.
local tag = nil
local last_report = ""
local SWITCH_TIMEOUT = 120 -- frames to wait for a switch before giving up
local JUMP = 30000 -- position change that counts as the copy jumping

local function p1_position()
  return mem.read(cfg.p1.x), mem.read(cfg.p1.y)
end

local function moved_from(pos)
  local x, y = p1_position()
  return x and pos.x and math.sqrt((x - pos.x) ^ 2 + (y - pos.y) ^ 2) > JUMP
end

local function start_tag(button, frames)
  local home = mem.read(ACTIVE)
  local x, y = p1_position()
  tag = {
    button = button,
    frames = frames,
    home = home,
    other = home == 0 and 1 or 0,
    phase = "switching",
    count = 0,
    from = { x = x, y = y },
    times = {},
  }
end

-- Release every DS button, except `button` if given.
local function set_pad(button)
  local pad = {}
  for _, name in ipairs(BUTTONS) do
    if pad_names[name] then
      pad[pad_names[name]] = name == button
    end
  end
  joypad.set(pad)
end

local function run_tag()
  if not tag then
    return
  end
  tag.count = tag.count + 1
  if tag.phase == "switching" then
    mem.write(ACTIVE, tag.other)
    set_pad(nil)
    if moved_from(tag.from) or tag.count >= SWITCH_TIMEOUT then
      tag.times.switch = moved_from(tag.from) and tag.count or "never"
      tag.phase, tag.count = "pressing", 0
      local x, y = p1_position()
      tag.at = { x = x, y = y }
    end
  elseif tag.phase == "pressing" then
    set_pad(tag.button)
    if tag.count >= tag.frames then
      tag.phase, tag.count = "returning", 0
    end
  else
    mem.write(ACTIVE, tag.home)
    set_pad(nil)
    if moved_from(tag.at) or tag.count >= SWITCH_TIMEOUT then
      tag.times.back = moved_from(tag.at) and tag.count or "never"
      last_report = string.format("Last tag-in: switch took %s frames, %s held %d, back took %s frames",
        tostring(tag.times.switch), tag.button, tag.frames, tostring(tag.times.back))
      console.log(last_report)
      tag = nil
    end
  end
end

local experiments = {
  { name = "Switch control to the other hero", run = function() mem.write(ACTIVE, mem.read(ACTIVE) == 0 and 1 or 0) end },
  { name = "Tag in, press A for 1 frame, back", run = function() start_tag("A", 1) end },
  { name = "Tag in, press A for 4 frames, back", run = function() start_tag("A", 4) end },
  { name = "Tag in, press A for 20 frames, back", run = function() start_tag("A", 20) end },
  { name = "Tag in, press B for 4 frames, back", run = function() start_tag("B", 4) end },
  { name = "Tag in, press Y for 4 frames, back", run = function() start_tag("Y", 4) end },
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
  gui.text(2, y + 34, tag and ("Tag-in: " .. tag.phase .. "...") or last_report)
  emu.frameadvance()
end
