-- Avatar: The Last Airbender (DS) co-op for BizHawk (melonDS core).
--
-- Player 1 plays normally on the emulator's controller. Player 2 takes over
-- a party member with their own keys or a second gamepad: every frame this
-- script reads Player 2's input and moves that character by writing to the
-- game's memory. Open it from Tools > Lua Console after loading the game.
--
-- The camera angle changes between levels, so "right" on screen can be any
-- direction in the game world. The script learns each screen direction by
-- watching which way Player 1's hero moves when they press it, and learns
-- how facing values work by watching the game turn Player 2's hero while
-- the computer walks it.

local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local cfg = dofile(here .. "config.lua")
local mem = dofile(here .. "memory.lua")

local EXPECTED_SHA1 = "B5BDC88489AD74399E2ED968D816669E594A5F74"
local DIRS = { "up", "down", "left", "right" }
local OPPOSITE = { up = "down", down = "up", left = "right", right = "left" }
-- Per-frame moves outside this range are ignored when learning: standing
-- still, or a jump such as a level load or cutscene.
local MIN_STEP, MAX_STEP = cfg.p2.speed / 3, cfg.p2.speed * 4
local FACING_SAMPLES = 40

if gameinfo.getromhash():upper() ~= EXPECTED_SHA1 then
  console.log("Warning: this doesn't look like Avatar - The Last Airbender (USA).")
  console.log("The addresses in config.lua may not match this version.")
end

-- What BizHawk calls the DS D-pad buttons (possibly with a "P1 " prefix).
local pad_names = {}
for key in pairs(joypad.get()) do
  for _, dir in ipairs(DIRS) do
    local name = dir:sub(1, 1):upper() .. dir:sub(2)
    if key == name or key:sub(-#name - 1) == " " .. name then
      pad_names[dir] = key
    end
  end
end

local function held(keys, names)
  for _, name in ipairs(names) do
    if keys[name] then
      return true
    end
  end
  return false
end

-- Keep (x, y) within the leash around Player 1. If the computer already put
-- Player 2 farther out, they can't go any farther, but they can slide along
-- the edge or come back.
local function leash(x, y, nx, ny, p1x, p1y)
  local function dist(ax, ay)
    return math.sqrt((ax - p1x) ^ 2 + (ay - p1y) ^ 2)
  end
  local limit = math.max(cfg.leash, dist(x, y))
  local d = dist(nx, ny)
  if d <= limit then
    return nx, ny
  end
  return p1x + (nx - p1x) * limit / d, p1y + (ny - p1y) * limit / d
end

-- Screen directions as unit vectors in the game world.
local screen = {}
local last_p1 = nil

local function learn_screen(p1x, p1y)
  if last_p1 then
    local dx, dy = p1x - last_p1.x, p1y - last_p1.y
    local len = math.sqrt(dx * dx + dy * dy)
    local pad, only, count = joypad.get(), nil, 0
    for _, dir in ipairs(DIRS) do
      if pad[pad_names[dir]] then
        only, count = dir, count + 1
      end
    end
    if count == 1 and len >= MIN_STEP and len <= MAX_STEP then
      screen[only] = { x = dx / len, y = dy / len }
    end
  end
  last_p1 = { x = p1x, y = p1y }
end

local function screen_vector(dir)
  local v = screen[dir]
  if v then
    return v
  end
  v = screen[OPPOSITE[dir]]
  return v and { x = -v.x, y = -v.y }
end

-- Facing model: facing = offset + sign * angle_of(direction), in units of
-- cfg.p2.facing_turn per full turn. Fitted from the computer's own moves.
local facing_samples = {}
local facing_model = nil
local TWO_PI = 2 * math.pi

local function angle_of(x, y)
  return math.atan(y, x) / TWO_PI * cfg.p2.facing_turn
end

local function circular_mean(values)
  local sx, sy = 0, 0
  for _, v in ipairs(values) do
    local a = v / cfg.p2.facing_turn * TWO_PI
    sx, sy = sx + math.cos(a), sy + math.sin(a)
  end
  return math.atan(sy, sx) / TWO_PI * cfg.p2.facing_turn, math.sqrt(sx * sx + sy * sy) / #values
end

local function fit_facing()
  if #facing_samples < FACING_SAMPLES / 2 then
    return
  end
  local best = nil
  for _, sign in ipairs({ 1, -1 }) do
    local offsets = {}
    for _, s in ipairs(facing_samples) do
      offsets[#offsets + 1] = s.face - sign * s.angle
    end
    local offset, agreement = circular_mean(offsets)
    if not best or agreement > best.agreement then
      best = { sign = sign, offset = offset, agreement = agreement }
    end
  end
  -- Only trust it when the samples agree to within roughly 25 degrees.
  facing_model = best.agreement > 0.9 and best or nil
end

local last_p2 = nil

local function learn_facing(x, y)
  local face = mem.read(cfg.p2.facing)
  if last_p2 and face then
    local dx, dy = x - last_p2.x, y - last_p2.y
    local len = math.sqrt(dx * dx + dy * dy)
    if len >= MIN_STEP and len <= MAX_STEP then
      table.insert(facing_samples, { angle = angle_of(dx, dy), face = face })
      if #facing_samples > FACING_SAMPLES then
        table.remove(facing_samples, 1)
      end
      fit_facing()
    end
  end
end

local function face_toward(vx, vy)
  local current = mem.read(cfg.p2.facing)
  if not facing_model or not current then
    return
  end
  local turn = cfg.p2.facing_turn
  local target = facing_model.offset + facing_model.sign * angle_of(vx, vy)
  -- Write the nearest equivalent of the target, so the value stays in
  -- whatever range the game itself uses.
  local diff = (target - current + turn / 2) % turn - turn / 2
  mem.write(cfg.p2.facing, current + diff)
end

-- Player 2's input as a world-space direction. Also reports whether any of
-- the pressed directions haven't been learned yet.
local function p2_vector(keys)
  local vx, vy, pressed, unknown = 0, 0, false, false
  for _, dir in ipairs(DIRS) do
    if held(keys, cfg.p2_keys[dir]) then
      pressed = true
      local v = screen_vector(dir)
      if v then
        vx, vy = vx + v.x, vy + v.y
      else
        unknown = true
      end
    end
  end
  local len = math.sqrt(vx * vx + vy * vy)
  if len > 0 then
    vx, vy = vx / len, vy / len
  end
  return vx, vy, pressed, unknown
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

-- One frame of co-op. Returns the status line to show.
local function step(keys)
  local p1x, p1y = mem.read(cfg.p1.x), mem.read(cfg.p1.y)
  local x, y = mem.read(cfg.p2.x), mem.read(cfg.p2.y)
  if not (p1x and p1y and x and y) then
    last_p1, last_p2 = nil, nil
    return "Co-op: waiting for the heroes"
  end
  learn_screen(p1x, p1y)

  local vx, vy, pressed, unknown = p2_vector(keys)
  if not pressed then
    -- The computer is steering Player 2's hero: learn how it faces.
    learn_facing(x, y)
  elseif vx ~= 0 or vy ~= 0 then
    local nx, ny = x + vx * cfg.p2.speed, y + vy * cfg.p2.speed
    if cfg.leash then
      nx, ny = leash(x, y, nx, ny, p1x, p1y)
    end
    mem.write(cfg.p2.x, nx)
    mem.write(cfg.p2.y, ny)
    if cfg.p2.facing then
      face_toward(vx, vy)
    end
  end
  last_p2 = { x = mem.read(cfg.p2.x), y = mem.read(cfg.p2.y) }
  run_actions(keys)

  if unknown then
    return "Co-op: P1, walk in every direction once so P2's controls can learn the camera"
  end
  return "Co-op: P2 ready" .. (cfg.p2.facing and not facing_model and " (learning facing)" or "")
end

local configured = cfg.p1.x and cfg.p1.y and cfg.p2.x and cfg.p2.y
if not configured then
  console.log("Co-op setup needed: fill in p1 and p2 in config.lua (see README.md).")
end

while true do
  local keys = input.get()

  if configured then
    gui.text(2, 2, step(keys))
  else
    gui.text(2, 2, "Co-op: setup needed - see README.md")
  end

  if cfg.show_host_input then
    gui.text(2, 18, host_input_text(keys))
  end

  emu.frameadvance()
end
