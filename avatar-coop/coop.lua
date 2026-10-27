-- Avatar: The Last Airbender (DS) co-op for BizHawk (melonDS core).
--
-- Player 1 plays normally on the emulator's controller. Player 2 takes over
-- one of the other heroes with their own keys or a second gamepad, and can
-- switch between heroes. Each hero's computer control (AI) can be turned on
-- or off. Every frame this script reads Player 2's input and moves their
-- hero by writing to the game's memory. Open it from Tools > Lua Console
-- after loading the game.
--
-- The camera angle changes between levels, so "right" on screen can be any
-- direction in the game world. The script learns each screen direction by
-- watching which way Player 1's hero moves when they press it, and learns
-- how facing values work by watching the game turn heroes while the
-- computer walks them.

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
local SPEED_SAMPLES = 30

if gameinfo.getromhash():upper() ~= EXPECTED_SHA1 then
  console.log("Warning: this doesn't look like Avatar - The Last Airbender (USA).")
  console.log("The addresses in config.lua may not match this version.")
end

-- What BizHawk calls the DS D-pad buttons (possibly with a "P1 " prefix).
local pad_names = {}
for key in pairs(joypad.get()) do
  for _, dir in ipairs(DIRS) do
    local name = dir:sub(1, 1):upper() .. dir:sub(2)
    if key == name or key == "P1 " .. name then
      pad_names[dir] = key
    end
  end
end

-- Heroes whose data location is known, with entries for each of their values.
local heroes = {}
for i, h in ipairs(cfg.heroes) do
  if h.at then
    local L = cfg.layout
    heroes[#heroes + 1] = {
      name = h.name,
      toggle = cfg.ai_toggle[i],
      x = mem.field(h.at, L.x),
      y = mem.field(h.at, L.y),
      facing = L.facing and mem.field(h.at, L.facing),
      pad = L.pad and mem.field(h.at, L.pad, 2),
      ai = true,
      last = nil, -- position after last frame, and whether Player 2 moved it
      anchor = nil, -- where the hero is held while its AI is off
    }
  end
end

local function held(keys, names)
  for _, name in ipairs(names or {}) do
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
-- Recent per-frame step lengths of Player 1's hero, so Player 2 moves at
-- the same speed.
local p1_steps = {}

local function walk_speed()
  if #p1_steps < 5 then
    return cfg.p2.speed
  end
  local sorted = { table.unpack(p1_steps) }
  table.sort(sorted)
  return sorted[math.ceil(#sorted / 2)]
end

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
      table.insert(p1_steps, len)
      if #p1_steps > SPEED_SAMPLES then
        table.remove(p1_steps, 1)
      end
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
-- cfg.p2.facing_turn per full turn. All heroes share it; it's fitted from
-- the computer's own moves.
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

-- Learn from a hero the computer is walking.
local function learn_facing(hero, x, y)
  local face = mem.read(hero.facing)
  if hero.last and not hero.last.steered and face then
    local dx, dy = x - hero.last.x, y - hero.last.y
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

local function face_toward(hero, vx, vy)
  local current = mem.read(hero.facing)
  if not facing_model or not current then
    return
  end
  local turn = cfg.p2.facing_turn
  local target = facing_model.offset + facing_model.sign * angle_of(vx, vy)
  -- Write the nearest equivalent of the target, so the value stays in
  -- whatever range the game itself uses.
  local diff = (target - current + turn / 2) % turn - turn / 2
  mem.write(hero.facing, current + diff)
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

-- Move Player 2's hero one step. The game's AI may keep walking the hero
-- too, which would add to Player 2's speed, so its moves are ignored except
-- ones against Player 2's direction: those are the game pushing the hero
-- back out of a wall.
local function steer(hero, x, y, vx, vy, p1x, p1y)
  local bx, by = x, y
  if hero.last and hero.last.steered and (x - hero.last.x) * vx + (y - hero.last.y) * vy >= 0 then
    bx, by = hero.last.x, hero.last.y
  end
  local speed = walk_speed()
  local nx, ny = bx + vx * speed, by + vy * speed
  if cfg.leash then
    nx, ny = leash(bx, by, nx, ny, p1x, p1y)
  end
  mem.write(hero.x, nx)
  mem.write(hero.y, ny)
  if hero.facing then
    face_toward(hero, vx, vy)
  end
end

-- Player 2's A/B/X/Y, written into their hero's own controller value. Each
-- held button flips the same bits it flips when held alone, so combinations
-- work whether the game uses 1 or 0 for "pressed". Returns whether any
-- button is held.
local function press_buttons(hero, keys)
  local v, value, any = cfg.p2.pad_values, cfg.p2.pad_values.none, false
  for _, b in ipairs({ "A", "B", "X", "Y" }) do
    if held(keys, cfg.p2_buttons[b]) then
      any = true
      if hero.pad then
        value = value ~ (v[b] ~ v.none)
      end
    end
  end
  if any and hero.pad then
    mem.write(hero.pad, value)
  end
  return any
end

-- With its AI off, a hero stays where it is unless Player 2 moves it.
local function hold_still(hero, x, y)
  hero.anchor = hero.anchor or { x = x, y = y }
  mem.write(hero.x, hero.anchor.x)
  mem.write(hero.y, hero.anchor.y)
  if hero.pad then
    mem.write(hero.pad, cfg.p2.pad_values.none)
  end
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

-- Player 2's hero, by index into `heroes`.
local p2_index = 1
for i, hero in ipairs(heroes) do
  if hero.name == cfg.p2.hero then
    p2_index = i
  end
end

-- The hero standing where Player 1's fixed position copy says (closest one,
-- in case the copy lags a frame behind).
local function p1_hero(p1x, p1y)
  local best, best_d = nil, MAX_STEP
  for i, hero in ipairs(heroes) do
    local x, y = mem.read(hero.x), mem.read(hero.y)
    if x and y then
      local d = math.sqrt((x - p1x) ^ 2 + (y - p1y) ^ 2)
      if d <= best_d then
        best, best_d = i, d
      end
    end
  end
  return best
end

-- Next hero for Player 2 after `from`, skipping Player 1's and missing ones.
local function next_hero(from, skip)
  for step = 1, #heroes do
    local i = (from + step - 1) % #heroes + 1
    if i ~= skip and mem.read(heroes[i].x) then
      return i
    end
  end
  return from
end

-- One frame of co-op. `pressed` holds keys that went down this frame.
-- Returns the lines to show.
local function step(keys, pressed)
  local p1x, p1y = mem.read(cfg.p1.x), mem.read(cfg.p1.y)
  if not (p1x and p1y) then
    last_p1 = nil
    return { "Co-op: waiting for the heroes" }
  end
  learn_screen(p1x, p1y)

  local p1 = p1_hero(p1x, p1y)
  if held(pressed, cfg.p2_switch) or p2_index == p1 then
    p2_index = next_hero(p2_index, p1)
  end
  for i, hero in ipairs(heroes) do
    if held(pressed, hero.toggle) then
      hero.ai, hero.anchor = not hero.ai, nil
    end
  end

  local vx, vy, moving, unknown = p2_vector(keys)
  local buttons = false
  for i, hero in ipairs(heroes) do
    local x, y = mem.read(hero.x), mem.read(hero.y)
    local steered = false
    if x and y and i ~= p1 then
      if i == p2_index and moving and (vx ~= 0 or vy ~= 0) then
        steer(hero, x, y, vx, vy, p1x, p1y)
        hero.anchor, steered = nil, true
      elseif hero.ai then
        learn_facing(hero, x, y)
      else
        hold_still(hero, x, y)
      end
      if i == p2_index then
        buttons = press_buttons(hero, keys)
      end
    end
    hero.last = x and { x = mem.read(hero.x), y = mem.read(hero.y), steered = steered }
  end
  run_actions(keys)

  local status = "Co-op: P2 ready" .. (facing_model and "" or " (learning facing)")
  if unknown then
    status = "Co-op: P1, walk in every direction once so P2's controls can learn the camera"
  elseif buttons and not cfg.layout.pad then
    status = "Co-op: P2's buttons aren't set up yet (run the finder's button search)"
  end
  local party = {}
  for i, hero in ipairs(heroes) do
    local role = i == p1 and "P1" or (i == p2_index and "P2" or "")
    party[#party + 1] = string.format("%d %s%s AI %s", i, hero.name, role ~= "" and " (" .. role .. ")" or "",
      hero.ai and "on" or "off")
  end
  return { status, "P2: " .. heroes[p2_index].name .. "  (H = switch hero)", table.concat(party, " | ") }
end

local configured = cfg.p1.x and cfg.p1.y and #heroes > 0
if not configured then
  console.log("Co-op setup needed: fill in p1 and heroes in config.lua (see README.md).")
end

local previous = {}
while true do
  local keys = input.get()
  local pressed = {}
  for name in pairs(keys) do
    if not previous[name] then
      pressed[name] = true
    end
  end
  previous = keys

  local lines = configured and step(keys, pressed) or { "Co-op: setup needed - see README.md" }
  if cfg.show_host_input then
    lines[#lines + 1] = host_input_text(keys)
  end
  for i, line in ipairs(lines) do
    gui.text(2, 2 + (i - 1) * 16, line)
  end

  emu.frameadvance()
end
