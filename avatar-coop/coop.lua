-- Avatar: The Last Airbender (DS) co-op for BizHawk (melonDS core).
--
-- Player 1 plays normally on the emulator's controller. Player 2 takes over
-- a party member with their own keys or a second gamepad: every frame this
-- script reads Player 2's input and moves that character by writing to the
-- game's memory. Open it from Tools > Lua Console after loading the game.

local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local cfg = dofile(here .. "config.lua")
local mem = dofile(here .. "memory.lua")

local EXPECTED_SHA1 = "B5BDC88489AD74399E2ED968D816669E594A5F74"

if gameinfo.getromhash():upper() ~= EXPECTED_SHA1 then
  console.log("Warning: this doesn't look like Avatar - The Last Airbender (USA).")
  console.log("The addresses in config.lua may not match this version.")
end

local function held(keys, names)
  for _, name in ipairs(names) do
    if keys[name] then
      return true
    end
  end
  return false
end

local function p2_direction(keys)
  local k = cfg.p2_keys
  local dx, dy = 0, 0
  if held(keys, k.left) then dx = dx - 1 end
  if held(keys, k.right) then dx = dx + 1 end
  if held(keys, k.up) then dy = dy - 1 end
  if held(keys, k.down) then dy = dy + 1 end
  return dx, dy
end

local function facing_for(dx, dy)
  local f = cfg.p2.facing_values
  if dx < 0 then return f.left end
  if dx > 0 then return f.right end
  if dy < 0 then return f.up end
  if dy > 0 then return f.down end
  return nil
end

local function clamp(v, lo, hi)
  return math.max(lo, math.min(hi, v))
end

-- Move Player 2 and keep them on the leash. Returns false when Player 2's
-- character isn't in memory right now (menus, cutscenes, loading).
local function move_p2(dx, dy)
  local x, y = mem.read(cfg.p2.x), mem.read(cfg.p2.y)
  if x == nil or y == nil then
    return false
  end

  if dx ~= 0 and dy ~= 0 then
    -- Keep diagonal movement the same speed as straight movement.
    dx, dy = dx * 0.7071, dy * 0.7071
  end
  local nx, ny = x + dx * cfg.p2.speed, y + dy * cfg.p2.speed

  local p1x, p1y = mem.read(cfg.p1.x), mem.read(cfg.p1.y)
  if cfg.leash and p1x and p1y then
    nx = clamp(nx, p1x - cfg.leash, p1x + cfg.leash)
    ny = clamp(ny, p1y - cfg.leash, p1y + cfg.leash)
  end

  if nx ~= x then mem.write(cfg.p2.x, nx) end
  if ny ~= y then mem.write(cfg.p2.y, ny) end

  local face = facing_for(dx, dy)
  if face ~= nil and cfg.p2.facing then
    mem.write(cfg.p2.facing, face)
  end
  return true
end

local function run_actions(keys)
  for _, action in pairs(cfg.actions) do
    if keys[action.button] then
      mem.write(action, action.value)
    end
  end
end

local function host_input_text(keys)
  local names = {}
  for name in pairs(keys) do
    names[#names + 1] = name
  end
  table.sort(names)
  return "Host input: " .. table.concat(names, " ")
end

local configured = cfg.p2.x ~= nil and cfg.p2.y ~= nil
if not configured then
  console.log("Co-op setup needed: fill in p2.x and p2.y in config.lua (see README.md).")
end

while true do
  local keys = input.get()

  if not configured then
    gui.text(2, 2, "Co-op: setup needed - see README.md")
  else
    local dx, dy = p2_direction(keys)
    if move_p2(dx, dy) then
      run_actions(keys)
      gui.text(2, 2, "Co-op: P2 ready")
    else
      gui.text(2, 2, "Co-op: waiting for P2's character")
    end
  end

  if cfg.show_host_input then
    gui.text(2, 18, host_input_text(keys))
  end

  emu.frameadvance()
end
