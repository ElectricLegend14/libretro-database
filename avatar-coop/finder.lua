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
--   P      run the button search: presses A, B, X and Y by itself and finds
--          values that follow them, such as the hero's own "controller"
--   1-4    label search: take a snapshot labelled with that number. Press
--          the number of the hero you're controlling (1 Aang, 2 Sokka,
--          3 Katara, 4 Haru), switch heroes, and repeat, at least twice per
--          hero. It keeps memory that always reads the same for the same
--          label and differs between labels.
-- Results are saved to finder-results.txt next to this script.

local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local cfg = dofile(here .. "config.lua")
local mem = dofile(here .. "memory.lua")

local DOMAIN = "Main RAM"
local RAM_SIZE = 0x400000
local HOLD = 20 -- frames to hold each direction
local SETTLE = 30 -- frames to wait for everyone to stop moving
local ROUNDS = 3
local SHOW = 8 -- candidates listed on screen

local status = "Stand in an open area, then press T (positions) or P (buttons)."
local candidates = {}
local current = 0
local test_state = nil
local button_results = nil
-- Label search state: the first snapshot in full, then candidate offsets and
-- the byte each one read for each label.
local labels = { count = 0, used = {}, base = nil, offs = nil, seen = {}, heroes = {} }
local MAX_LABEL_RESULTS = 300

-- The DS has one controller; find what BizHawk calls its buttons.
local buttons = {}
for key in pairs(joypad.get()) do
  for _, name in ipairs({ "Up", "Down", "Left", "Right", "A", "B", "X", "Y" }) do
    if key == name or key == "P1 " .. name then
      buttons[name] = key
    end
  end
end

local held_text = ""

local function draw()
  gui.text(2, 2, status)
  -- Shows the names BizHawk gives the keys being pressed, in case a key
  -- isn't recognised and its name needs adding to config.lua.
  gui.text(2, 2 + (SHOW + 2) * 14, held_text)
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

-- All of Main RAM as a byte table, and the index of its first byte.
local function ram_bytes()
  if memory.read_bytes_as_array then
    return memory.read_bytes_as_array(0, RAM_SIZE, DOMAIN), 1
  end
  return memory.readbyterange(0, RAM_SIZE, DOMAIN), 0
end

-- Every 2-byte-aligned unsigned 16-bit value in Main RAM, keyed by offset.
local function read_all16()
  local bytes, base = ram_bytes()
  local values = {}
  for off = 0, RAM_SIZE - 2, 2 do
    values[off] = bytes[off + base] + bytes[off + base + 1] * 0x100
  end
  return values
end

-- Every 4-byte-aligned signed value in Main RAM, keyed by offset.
local function read_all()
  local bytes, base = ram_bytes()
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
  if button_results then
    f:write("\nButtons (16-bit value with each held)\n")
    f:write("address     none    A       B       X       Y\n")
    for _, r in ipairs(button_results.found) do
      f:write(string.format("0x%08X  0x%04X  0x%04X  0x%04X  0x%04X  0x%04X\n", 0x02000000 + r.off,
        r.values.none, r.values.A, r.values.B, r.values.X, r.values.Y))
    end
    f:write("\nControlled hero's data (x address):\n")
    for _, off in ipairs(button_results.heroes) do
      f:write(string.format("  0x%08X\n", 0x02000000 + off))
    end
  end
  if labels.count > 0 then
    local names = {}
    for name in pairs(labels.used) do
      names[#names + 1] = name
    end
    table.sort(names)
    f:write(string.format("\nLabel search (%d snapshots)\n", labels.count))
    for _, name in ipairs(names) do
      f:write("Controlled hero's data for label " .. name .. ":")
      for _, off in ipairs(labels.heroes[name]) do
        f:write(string.format(" 0x%08X", 0x02000000 + off))
      end
      f:write("\n")
    end
    f:write("address     " .. table.concat(names, "     ") .. "\n")
    local shown = 0
    for _, off in ipairs(labels.offs or {}) do
      -- Only values that tell every label apart.
      local seen, distinct, count, line = labels.seen[off], {}, 0, ""
      for _, name in ipairs(names) do
        local v = seen[name]
        if v ~= nil and not distinct[v] then
          distinct[v], count = true, count + 1
        end
        line = line .. (v and string.format("  0x%02X", v) or "  ----")
      end
      if count == #names and shown < MAX_LABEL_RESULTS then
        f:write(string.format("0x%08X%s\n", 0x02000000 + off, line))
        shown = shown + 1
      end
    end
  end
  f:close()
end

-- Each button is pressed twice, in a different order, so values that merely
-- happen to change (timers, animations) are ruled out.
local BUTTON_PATTERNS = { "none", "A", "B", "X", "Y", "none", "Y", "X", "B", "A", "none" }
local BUTTON_HOLD = 4 -- frames held before reading
local BUTTON_REST = 40 -- frames between presses, to let attacks finish
local MAX_BUTTON_RESULTS = 40

local function button_search()
  local seen, offs, baseline = {}, nil, nil
  for i, name in ipairs(BUTTON_PATTERNS) do
    status = string.format("Button search %d of %d - hands off!", i, #BUTTON_PATTERNS)
    wait(BUTTON_REST)
    local pad = name ~= "none" and { [buttons[name]] = true } or nil
    for _ = 1, BUTTON_HOLD do
      frame(pad)
    end
    if not offs and i == 1 then
      baseline = read_all16()
    elseif not offs then
      -- Keep only what differs between nothing held and A held.
      offs = {}
      for off, v in pairs(read_all16()) do
        if v ~= baseline[off] then
          offs[#offs + 1] = off
          seen[off] = { none = baseline[off], [name] = v }
        end
      end
      baseline = nil
    else
      local kept = {}
      for _, off in ipairs(offs) do
        local v = memory.read_u16_le(off, DOMAIN)
        local s = seen[off]
        if s[name] == nil or s[name] == v then
          s[name] = v
          kept[#kept + 1] = off
        end
      end
      offs = kept
    end
  end
  -- A real controller gives each button its own value.
  local found = {}
  for _, off in ipairs(offs) do
    local s, distinct = seen[off], {}
    for _, name in ipairs({ "none", "A", "B", "X", "Y" }) do
      if s[name] ~= nil then
        distinct[s[name]] = true
      end
    end
    local count = 0
    for _ in pairs(distinct) do
      count = count + 1
    end
    if count == 5 and #found < MAX_BUTTON_RESULTS then
      found[#found + 1] = { off = off, values = s }
    end
  end
  table.sort(found, function(a, b)
    return a.off < b.off
  end)
  return found
end

-- Where the controlled hero's own data is: every x/depth pair matching the
-- fixed Player 1 copy, so button addresses can be turned into offsets.
local function find_heroes()
  local x, y = mem.read(cfg.p1.x), mem.read(cfg.p1.y)
  local fixed = mem.resolve(cfg.p1.x)
  local heroes = {}
  if not x then
    return heroes
  end
  local values = read_all()
  for off, v in pairs(values) do
    if v == x and values[off + 8] == y and off ~= fixed then
      heroes[#heroes + 1] = off
    end
  end
  table.sort(heroes)
  return heroes
end

-- One label search snapshot. Until two different labels have been seen, the
-- latest snapshot is kept in full; after that only differing bytes are kept,
-- and dropped as soon as one reads differently for a label it had before.
local function label_snapshot(name)
  local s = labels
  s.count, s.used[name] = s.count + 1, true
  s.heroes[name] = s.heroes[name] or find_heroes()
  if s.offs then
    local kept = {}
    for _, off in ipairs(s.offs) do
      local v, seen = memory.read_u8(off, DOMAIN), s.seen[off]
      if seen[name] == nil or seen[name] == v then
        seen[name] = v
        kept[#kept + 1] = off
      else
        s.seen[off] = nil
      end
    end
    s.offs = kept
  elseif s.base and s.base.name ~= name then
    local bytes, base = ram_bytes()
    local old = s.base
    s.offs = {}
    for off = 0, RAM_SIZE - 1 do
      local a, b = old.bytes[off + old.start], bytes[off + base]
      if a ~= b then
        s.offs[#s.offs + 1] = off
        s.seen[off] = { [old.name] = a, [name] = b }
      end
    end
    s.base = nil
  else
    local bytes, base = ram_bytes()
    s.base = { bytes = bytes, start = base, name = name }
  end
  save_results()
  status = string.format("Snapshot %d saved as label %s. %s", s.count, name,
    s.offs and (#s.offs .. " values left.") or "Now switch and press another number.")
  console.log(status)
end

local function run_button_search()
  local start = memorysavestate.savecorestate()
  local found = button_search()
  memorysavestate.loadcorestate(start)
  memorysavestate.removestate(start)
  button_results = { found = found, heroes = find_heroes() }
  save_results()
  status = #found .. " button values saved to finder-results.txt."
  console.log(status)
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

-- The number pressed for a label snapshot, using the same key names as the
-- AI toggles in config.lua.
local function label_pressed(pressed)
  for i, names in ipairs(cfg.ai_toggle) do
    for _, k in ipairs(names) do
      if pressed(k) then
        return tostring(i)
      end
    end
  end
  return nil
end

local previous = {}
while true do
  local keys = input.get()
  local function pressed(k)
    return keys[k] and not previous[k]
  end
  local names = {}
  for name in pairs(keys) do
    names[#names + 1] = name
  end
  table.sort(names)
  held_text = #names > 0 and "Keys held: " .. table.concat(names, " ") or ""
  local label = label_pressed(pressed)
  if buttons.Right and pressed("T") then
    run_search()
  elseif buttons.A and pressed("P") then
    run_button_search()
  elseif label then
    label_snapshot(label)
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
