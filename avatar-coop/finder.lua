-- Finds the position addresses coop.lua needs, so you don't have to search
-- by hand. It walks Player 1 left/right and up/down by itself and keeps the
-- memory values that follow those moves: Player 1's position, the party
-- members following them, and usually a few copies (camera, graphics).
--
-- How to use: during normal play, stand in an open area away from walls and
-- enemies, then run this from the Lua Console. Don't touch the controls while
-- it searches. Keys (keyboard):
--   T      run the automatic search (about 20 seconds)
--   N / B  teleport-test the next / previous candidate
--   M      mark the candidate being tested as a real position
-- Results are saved to finder-results.txt next to this script.

local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""

local DOMAIN = "Main RAM"
local RAM_SIZE = 0x400000
local HOLD = 20 -- frames to hold each direction
local SETTLE = 30 -- frames to wait for everyone to stop moving
local ROUNDS = 3
local SHOW = 8 -- candidates listed on screen

local status = "Stand in an open area, then press T to search."
local candidates = {}
local current = 0
local test_state = nil

-- The DS has one controller; find what BizHawk calls its D-pad buttons.
local buttons = {}
for key in pairs(joypad.get()) do
  for _, name in ipairs({ "Up", "Down", "Left", "Right" }) do
    if key == name or key:sub(-#name - 1) == " " .. name then
      buttons[name] = key
    end
  end
end

local function draw()
  gui.text(2, 2, status)
  local first = math.max(1, math.min(current - 3, #candidates - SHOW + 1))
  for i = first, math.min(#candidates, first + SHOW - 1) do
    local c = candidates[i]
    local line = string.format("%s%02d %08X %-5s%s", i == current and ">" or " ", i,
      0x02000000 + c.off, c.axis, c.marked and "  MATCH" or "")
    gui.text(2, 20 + (i - first) * 14, line)
  end
end

local function frame(pad)
  if pad then
    joypad.set(pad)
  end
  draw()
  emu.frameadvance()
end

local function hold(name, n)
  for _ = 1, n do
    frame({ [buttons[name]] = true })
  end
end

local function wait(n)
  for _ = 1, n do
    frame()
  end
end

-- Every 4-byte-aligned signed value in Main RAM, keyed by offset.
local function read_all()
  local bytes, base
  if memory.read_bytes_as_array then
    bytes, base = memory.read_bytes_as_array(0, RAM_SIZE, DOMAIN), 1
  else
    bytes, base = memory.readbyterange(0, RAM_SIZE, DOMAIN), 0
  end
  local values = {}
  for off = 0, RAM_SIZE - 4, 4 do
    local i = off + base
    local v = bytes[i] + bytes[i + 1] * 0x100 + bytes[i + 2] * 0x10000 + bytes[i + 3] * 0x1000000
    if v >= 0x80000000 then
      v = v - 0x100000000
    end
    values[off] = v
  end
  return values
end

local function read_some(offs)
  local values = {}
  for _, off in ipairs(offs) do
    values[off] = memory.read_s32_le(off, DOMAIN)
  end
  return values
end

local function narrow(before, after, keep)
  local kept = {}
  for off, v in pairs(after) do
    if keep(off, v - before[off]) then
      kept[#kept + 1] = off
    end
  end
  table.sort(kept)
  return kept
end

-- Offsets whose value moves one way while `plus` is held, the opposite way
-- while `minus` is held, and stays still when nothing is held. Also returns
-- how far each one moved, used to size the teleport test.
local function search_axis(plus, minus)
  local offs, up, moved = nil, {}, {}
  local function snapshot()
    return offs and read_some(offs) or read_all()
  end
  for round = 1, ROUNDS do
    status = string.format("Searching %s/%s, round %d of %d - hands off!", plus, minus, round, ROUNDS)
    wait(SETTLE)
    local a = snapshot()
    hold(plus, HOLD)
    local b = snapshot()
    offs = narrow(a, b, function(off, d)
      if d == 0 then
        return false
      end
      if up[off] == nil then
        up[off], moved[off] = d > 0, math.abs(d)
      end
      return (d > 0) == up[off]
    end)
    hold(minus, HOLD)
    local c = read_some(offs)
    offs = narrow(b, c, function(off, d)
      return d ~= 0 and (d > 0) ~= up[off]
    end)
    wait(SETTLE)
    local e = read_some(offs)
    wait(SETTLE)
    offs = narrow(e, read_some(offs), function(_, d)
      return d == 0
    end)
  end
  return offs, moved
end

local function save_results()
  local f = io.open(here .. "finder-results.txt", "w")
  if not f then
    console.log("Couldn't write finder-results.txt")
    return
  end
  f:write("Avatar co-op finder results\n")
  f:write("axis  address     moved in " .. HOLD .. " frames\n")
  for _, c in ipairs(candidates) do
    f:write(string.format("%-5s 0x%08X  %d%s\n", c.axis, 0x02000000 + c.off, c.moved,
      c.marked and "  MATCH" or ""))
  end
  for _, c in ipairs(candidates) do
    if c.marked and c.pointers then
      f:write(string.format("\nPointers near 0x%08X (pointer -> offset):\n", 0x02000000 + c.off))
      for _, p in ipairs(c.pointers) do
        f:write(string.format("  0x%08X -> 0x%X\n", 0x02000000 + p.at, p.offset))
      end
    end
  end
  f:close()
end

-- Positions in heap memory move between levels, but something always points
-- at the character's data. List every value pointing up to POINTER_RANGE
-- bytes before `off`; comparing two levels shows which pointer is stable.
local POINTER_RANGE = 0x400
local MAX_POINTERS = 60
local function find_pointers(off)
  local target = 0x02000000 + off
  local found = {}
  for at, v in pairs(read_all()) do
    if v >= target - POINTER_RANGE and v <= target then
      found[#found + 1] = { at = at, offset = target - v }
    end
  end
  table.sort(found, function(a, b)
    return a.at < b.at
  end)
  while #found > MAX_POINTERS do
    table.remove(found)
  end
  return found
end

local function test(i)
  current = i
  local c = candidates[i]
  memorysavestate.loadcorestate(test_state)
  -- Jump about three steps' worth. A real position teleports its character;
  -- a copy (camera, graphics) snaps back or does nothing visible.
  memory.write_s32_le(c.off, memory.read_s32_le(c.off, DOMAIN) + c.moved * 3, DOMAIN)
  status = string.format("Testing %d of %d: who jumped? M = mark it, N/B = next/previous", i, #candidates)
end

local function run_search()
  if test_state then
    memorysavestate.removestate(test_state)
  end
  test_state = memorysavestate.savecorestate()
  candidates, current = {}, 0
  for _, axis in ipairs({ { "x", "Right", "Left" }, { "depth", "Down", "Up" } }) do
    local offs, moved = search_axis(axis[2], axis[3])
    memorysavestate.loadcorestate(test_state)
    for _, off in ipairs(offs) do
      candidates[#candidates + 1] = { off = off, axis = axis[1], moved = moved[off] }
    end
  end
  save_results()
  if #candidates == 0 then
    status = "Nothing found. Move somewhere more open and press T again."
  else
    status = #candidates .. " candidates saved. Press N to teleport-test them."
  end
  console.log(status)
end

if not buttons.Right then
  status = "Couldn't find the D-pad in joypad.get() - is a DS game loaded?"
end

local previous = {}
while true do
  local keys = input.get()
  local function pressed(k)
    return keys[k] and not previous[k]
  end
  if buttons.Right and pressed("T") then
    run_search()
  elseif #candidates > 0 and pressed("N") then
    test(current % #candidates + 1)
  elseif #candidates > 0 and pressed("B") then
    test(current <= 1 and #candidates or current - 1)
  elseif current > 0 and pressed("M") then
    local c = candidates[current]
    c.marked = not c.marked
    if c.marked and not c.pointers then
      status = "Looking for pointers to this one..."
      draw()
      c.pointers = find_pointers(c.off)
      status = string.format("Marked, %d pointers saved. N/B = next/previous", #c.pointers)
    end
    save_results()
  end
  previous = keys
  frame()
end
